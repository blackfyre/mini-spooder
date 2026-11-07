// Package app is the pak's run loop without SDL (openspec change
// handheld-pak, designs D2 and D5): the pad's presses in, the desired state
// (requested motion and motion-request counter) out in every command frame,
// the request rules (presses only on a live link, one new request per frame,
// no motion request after an unconfirmed stop), hold-to-repeat for the
// walking controls, the leave state machine and the screen model.
// cmd/minispooder only feeds it the controls and the Menu and Power presses
// and draws its Screen.
package app

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/motions"
	"github.com/blackfyre/mini-spooder/handheld/internal/padmap"
	"github.com/blackfyre/mini-spooder/handheld/internal/protocol"
	"github.com/blackfyre/mini-spooder/handheld/internal/udplink"
	"github.com/blackfyre/mini-spooder/handheld/internal/wlan"
)

const (
	// LeaveTimeout is the longest a leave waits for Standby to finish,
	// from the press that started it.
	LeaveTimeout = 3 * time.Second
	// ConfirmTimeout is the time after which a request with no echo is
	// marked NOT CONFIRMED.
	ConfirmTimeout = time.Second
	// LiveMaxAge is the oldest the newest telemetry frame may be for the
	// link to be live.
	LiveMaxAge = 250 * time.Millisecond
	// LiveFrames is the telemetry frames that must arrive after the first
	// command frame of a link start for the link to be live.
	LiveFrames = 2
	// StaleAfter marks the robot's values stale without a frame taken as
	// newest.
	StaleAfter = 500 * time.Millisecond
	// SuspendScript is NextUI's suspend on the tg5040; it returns on
	// resume.
	SuspendScript = "/mnt/SDCARD/.system/tg5040/bin/suspend"
	// PowerOffCommand powers the device off.
	PowerOffCommand = "/sbin/poweroff"
)

// Action is what a leave does once its wait ends.
type Action int

const (
	ActionNone     Action = iota
	ActionExit            // Menu
	ActionSuspend         // a short Power press
	ActionPowerOff        // Power held 2 s
)

func (a Action) String() string {
	switch a {
	case ActionExit:
		return "exit"
	case ActionSuspend:
		return "suspend"
	case ActionPowerOff:
		return "power off"
	}
	return "none"
}

// State is the leave state machine's state.
type State int

const (
	Driving State = iota
	Leaving       // Standby requested, waiting for it to finish
	Acting        // the wait is over: frames stopped, the action runs
	Done          // the run has ended
)

// Status is a request's status as the screen shows it.
type Status int

const (
	StatusNone Status = iota
	StatusNotSent
	StatusRequested
	StatusTaken
	StatusRejected
	StatusNotTaken
	StatusNotConfirmed
)

func (s Status) String() string {
	switch s {
	case StatusNotSent:
		return "NOT SENT"
	case StatusRequested:
		return "REQUESTED"
	case StatusTaken:
		return "TAKEN"
	case StatusRejected:
		return "REJECTED"
	case StatusNotTaken:
		return "NOT TAKEN"
	case StatusNotConfirmed:
		return "NOT CONFIRMED"
	}
	return ""
}

// Link is what the app needs of udplink.Link.
type Link interface {
	TakeEvents() []udplink.Event
	Status(now time.Time) udplink.Status
	Quiet(now time.Time) bool
	Resume(now time.Time)
}

// PowerSave is what the app needs of wlan.PowerSave.
type PowerSave interface {
	Off(ctx context.Context) error
	Restore(ctx context.Context) error
}

// request is one request the pak has made.
type request struct {
	motion  uint8 // 0 for a stop
	stop    bool
	counter uint8
	at      time.Time
	sent    bool // a frame carrying it has been sent
	status  Status
	// seen: a frame echoing its counter with result accepted reported the
	// motion playing or pending; ended: a later one reported it neither.
	seen, ended bool
}

// shown is the last press as the screen shows it.
type shown struct {
	have    bool
	motion  uint8
	stop    bool
	notSent bool
}

// gate is the live-link gate (design D2): at least LiveFrames telemetry
// frames after the first command frame of the current link start.
type gate struct {
	waiting bool      // no command frame sent since the link start
	first   time.Time // when that first frame was sent
	frames  int
}

// leave is one leave in progress (design D5).
type leave struct {
	action Action
	since  time.Time
	want   bool  // Standby is still to be requested
	armed  bool  // the newest request is the leave's Standby
	c      uint8 // its counter value
	done   bool  // Standby seen to finish
}

// repeat is hold-to-repeat's state (design D2).
type repeat struct {
	active  bool
	control padmap.Buttons
	motion  uint8
}

// App is the run's state. Its methods are safe for concurrent use: the
// sender calls Step, the input loop SetControls and Press, the screen
// Screen.
type App struct {
	link     Link
	run      wlan.Runner
	power    PowerSave                    // nil off the device
	readFile func(string) ([]byte, error) // nil off the device: no signal level
	clock    func() time.Time

	mu           sync.Mutex
	controls     padmap.Controls
	cmd          protocol.Command
	newest       *request
	shown        shown
	stopAwaiting bool
	gate         gate
	rep          repeat
	state        State
	leave        leave
	done         Action
	notice       string
	signalKnown  bool
	signalDBm    int
}

// New returns an app driving through link. run, power and readFile may be
// nil off the device.
func New(link Link, run wlan.Runner, power PowerSave, readFile func(string) ([]byte, error)) *App {
	return &App{link: link, run: run, power: power, readFile: readFile, clock: time.Now, gate: gate{waiting: true}}
}

// SignalPeriod is how often the run samples the signal level.
const SignalPeriod = time.Second

// signalTimeout bounds one sample's iw.
const signalTimeout = 500 * time.Millisecond

// SampleSignal reads wlan0's signal level for the screen. Off the device it
// does nothing; a failed read makes the level unknown.
func (a *App) SampleSignal(ctx context.Context) {
	if a.readFile == nil || a.run == nil {
		return
	}
	ctx, cancel := context.WithTimeout(ctx, signalTimeout)
	defer cancel()
	dbm, err := wlan.Signal(ctx, a.run, a.readFile)
	a.mu.Lock()
	a.signalKnown, a.signalDBm = err == nil, dbm
	a.mu.Unlock()
}

// live tells whether the link is live at now (design D2).
func (a *App) live(now time.Time) bool {
	if a.gate.waiting || a.gate.frames < LiveFrames {
		return false
	}
	st := a.link.Status(now)
	return st.HaveNewest && now.Sub(st.NewestAt) <= LiveMaxAge
}

// restartGate makes the link not live until LiveFrames telemetry frames
// have arrived after the next command frame: at a resume and when the link
// is lost.
func (a *App) restartGate() {
	a.gate = gate{waiting: true}
	a.rep.active = false
}

// pump takes the received frames into the state, then the timeouts.
func (a *App) pump(now time.Time) {
	for _, ev := range a.link.TakeEvents() {
		if ev.Late {
			continue // counted late and not shown: it decides nothing
		}
		if !a.gate.waiting && !ev.At.Before(a.gate.first) {
			a.gate.frames++
		}
		a.observe(ev.Telemetry, now)
	}
	if n := a.newest; n != nil && n.status == StatusRequested && now.Sub(n.at) >= ConfirmTimeout {
		n.status = StatusNotConfirmed
		if n.stop {
			a.stopAwaiting = false
		}
	}
	if !a.live(now) {
		// Lost after it was live: the robot may end the session, so the
		// gate starts again. Repetition ends whenever the link is not live.
		if !a.gate.waiting && a.gate.frames >= LiveFrames {
			a.restartGate()
		}
		a.rep.active = false
	}
}

// observe takes one telemetry frame taken as newest.
func (a *App) observe(t protocol.Telemetry, now time.Time) {
	n := a.newest
	if n == nil || !n.sent || t.Counter != n.counter {
		return
	}
	if n.status == StatusRequested || n.status == StatusNotConfirmed {
		switch t.Result {
		case protocol.ResultNone:
			n.status = StatusNotTaken
		case protocol.ResultAccepted, protocol.ResultStop:
			n.status = StatusTaken
		case protocol.ResultRejected:
			n.status = StatusRejected
		}
	}
	if n.stop {
		a.stopAwaiting = false
		return
	}
	if t.Result != protocol.ResultAccepted {
		if a.rep.active && a.rep.motion == n.motion {
			a.rep.active = false
		}
		return
	}
	if t.Playing == n.motion || t.Pending == n.motion {
		n.seen = true
		return
	}
	if !n.seen || n.ended {
		return
	}
	n.ended = true
	// The leave's Standby has finished.
	if a.state == Leaving && a.leave.armed && n.counter == a.leave.c && n.motion == motions.Standby {
		a.leave.done = true
		return
	}
	// A held walking control's request has ended: request it again.
	if a.state == Driving && a.rep.active && a.rep.motion == n.motion &&
		a.controls.Buttons&a.rep.control != 0 && a.live(now) {
		a.request(n.motion, false, now, true)
	}
}

// request makes a request: motion, or a stop. A request made before a
// frame carried the previous one replaces it, keeping its counter value, so
// a frame never carries two.
func (a *App) request(motion uint8, stop bool, now time.Time, repetition bool) {
	counter := a.cmd.Counter + 1
	if a.newest != nil && !a.newest.sent {
		counter = a.newest.counter
	}
	a.cmd = protocol.Command{Motion: motion, Counter: counter}
	a.newest = &request{motion: motion, stop: stop, counter: counter, at: now, status: StatusRequested}
	a.shown = shown{have: true, motion: motion, stop: stop}
	if stop {
		a.stopAwaiting = true
	}
	if !repetition {
		a.rep.active = false
	}
}

// notSent shows a press that was not taken.
func (a *App) notSent(ev padmap.Event) {
	a.shown = shown{have: true, motion: ev.Motion, stop: ev.Stop, notSent: true}
}

// press takes one press from the mapping.
func (a *App) press(ev padmap.Event, now time.Time) {
	switch a.state {
	case Driving:
	case Leaving:
		if !ev.Stop {
			return // only B stays live during the leave's wait
		}
	default:
		return
	}
	if !a.live(now) {
		a.notSent(ev)
		return
	}
	if ev.Stop {
		a.request(motions.Stop, true, now, false)
		if a.state == Leaving {
			// Disarmed until Standby is requested again (design D5).
			a.leave.armed, a.leave.want, a.leave.done = false, true, false
		}
		return
	}
	if (a.newest != nil && !a.newest.sent) || a.stopAwaiting {
		a.notSent(ev)
		return
	}
	a.request(ev.Motion, false, now, false)
	if ev.Walking {
		a.rep = repeat{active: true, control: ev.Control, motion: ev.Motion}
	}
}

// advance requests the leave's Standby once the rules allow it: after an
// unconfirmed stop it waits; an unsent motion request it replaces. It is
// sent whether or not the link is live.
func (a *App) advance(now time.Time) {
	if a.state != Leaving || !a.leave.want || a.stopAwaiting {
		return
	}
	a.request(motions.Standby, false, now, false)
	a.leave.want, a.leave.armed, a.leave.c = false, true, a.cmd.Counter
}

// SetControls takes the pad's state at now: one request per press, and the
// end of repetition on release.
func (a *App) SetControls(c padmap.Controls, now time.Time) {
	a.mu.Lock()
	defer a.mu.Unlock()
	// The new state first: a release ends repetition before any queued
	// telemetry could repeat the step.
	events := padmap.Events(a.controls, c)
	a.controls = c
	if a.rep.active && c.Buttons&a.rep.control == 0 {
		a.rep.active = false
	}
	a.pump(now)
	for _, ev := range events {
		a.press(ev, now)
	}
	a.advance(now)
}

// Press starts a leave with act (Menu: ActionExit; Power: ActionSuspend or
// ActionPowerOff). A press while a leave is in progress is ignored.
func (a *App) Press(act Action, now time.Time) {
	if act == ActionNone {
		return
	}
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.state != Driving {
		return
	}
	a.pump(now)
	a.state = Leaving
	a.leave = leave{action: act, since: now, want: true}
	a.rep.active = false
	a.advance(now)
}

// Step is one command period at now: it takes the received frames, advances
// the leave, and returns the command and whether to send it. It runs a
// leave's action itself, so a suspend blocks it until the resume.
func (a *App) Step(now time.Time) (protocol.Command, udplink.Tick) {
	a.mu.Lock()
	if a.state == Done || a.state == Acting {
		a.mu.Unlock()
		return protocol.Command{}, udplink.Stop
	}
	a.pump(now)
	a.advance(now)
	if a.state == Leaving && (a.leave.done || now.Sub(a.leave.since) >= LeaveTimeout) {
		a.state = Acting
		act := a.leave.action
		a.mu.Unlock()
		return a.act(act)
	}
	cmd := a.cmd
	if a.link.Quiet(now) {
		a.mu.Unlock()
		return cmd, udplink.Skip
	}
	if a.newest != nil {
		a.newest.sent = true
	}
	if a.gate.waiting {
		a.gate = gate{first: now}
	}
	a.mu.Unlock()
	return cmd, udplink.Send
}

func (a *App) setNotice(format string, args ...any) {
	a.mu.Lock()
	a.notice = fmt.Sprintf(format, args...)
	a.mu.Unlock()
}

// restorePower restores the power-saving setting before a suspend or a
// power-off, which leave the process no chance to run its deferred
// restore.
func (a *App) restorePower(ctx context.Context) {
	if a.power == nil {
		return
	}
	if err := a.power.Restore(ctx); err != nil {
		a.setNotice("%v", err)
	}
}

// act runs a leave's action, without the lock held, once the frames have
// stopped.
func (a *App) act(act Action) (protocol.Command, udplink.Tick) {
	ctx := context.Background()
	switch act {
	case ActionSuspend:
		a.restorePower(ctx)
		if a.run != nil {
			if _, err := a.run.Run(ctx, SuspendScript); err != nil {
				a.setNotice("suspend: %v", err)
			}
		}
		if a.power != nil {
			if err := a.power.Off(ctx); err != nil {
				a.setNotice("%v", err)
			}
		}
		return a.resume()
	case ActionPowerOff:
		a.restorePower(ctx)
		if a.run != nil {
			if _, err := a.run.Run(ctx, PowerOffCommand); err != nil {
				a.setNotice("power off: %v", err)
			}
		}
	}
	a.mu.Lock()
	a.state, a.done = Done, act
	a.mu.Unlock()
	return protocol.Command{}, udplink.Stop
}

// resume returns to driving after a suspend: the silence again, the
// sequence numbers rebaselined, and no press taken until the link is live.
func (a *App) resume() (protocol.Command, udplink.Tick) {
	a.mu.Lock()
	defer a.mu.Unlock()
	now := a.clock()
	a.link.Resume(now)
	a.restartGate()
	a.state = Driving
	a.leave = leave{}
	return a.cmd, udplink.Skip
}

// State is the leave state machine's state.
func (a *App) State() State {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.state
}

// Ended is the action that ended the run, ActionNone while it runs.
func (a *App) Ended() Action {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.done
}

// Command is the desired state the next frame carries.
func (a *App) Command() protocol.Command {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.cmd
}
