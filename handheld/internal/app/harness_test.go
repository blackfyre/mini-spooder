package app

import (
	"context"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/padmap"
	"github.com/blackfyre/mini-spooder/handheld/internal/protocol"
	"github.com/blackfyre/mini-spooder/handheld/internal/udplink"
)

// fakeLink is the link with a real receiver, fed by the test, and an
// injected silence.
type fakeLink struct {
	rx      *udplink.Receiver
	events  []udplink.Event
	quiet   time.Time
	resumes []time.Time
}

func (f *fakeLink) TakeEvents() []udplink.Event {
	ev := f.events
	f.events = nil
	return ev
}
func (f *fakeLink) Status(now time.Time) udplink.Status { return f.rx.Status(now) }
func (f *fakeLink) Quiet(now time.Time) bool            { return now.Before(f.quiet) }
func (f *fakeLink) Resume(now time.Time) {
	f.quiet = now.Add(udplink.StartSilence)
	f.rx.Rebaseline()
	f.events = nil
	f.resumes = append(f.resumes, now)
}

// fakeRun records commands; block, when set, runs inside each.
type fakeRun struct {
	mu    sync.Mutex
	calls []string
	block func(cmd string)
}

func (r *fakeRun) Run(_ context.Context, name string, args ...string) ([]byte, error) {
	cmd := strings.Join(append([]string{name}, args...), " ")
	r.mu.Lock()
	r.calls = append(r.calls, cmd)
	r.mu.Unlock()
	if r.block != nil {
		r.block(cmd)
	}
	return nil, nil
}

// fakePower logs Off and Restore into the same log as the runner.
type fakePower struct{ run *fakeRun }

func (p fakePower) Off(context.Context) error {
	p.run.mu.Lock()
	p.run.calls = append(p.run.calls, "power off")
	p.run.mu.Unlock()
	return nil
}
func (p fakePower) Restore(context.Context) error {
	p.run.mu.Lock()
	p.run.calls = append(p.run.calls, "power restore")
	p.run.mu.Unlock()
	return nil
}

// harness drives an app with a fake link and a fake clock.
type harness struct {
	t      *testing.T
	t0     time.Time
	now    time.Time
	link   *fakeLink
	app    *App
	run    *fakeRun
	pad    padmap.Controls
	telSeq uint8
	// sent is every frame Step asked to send, with its time.
	sent []sentFrame
}

type sentFrame struct {
	cmd protocol.Command
	at  time.Time
}

func newHarness(t *testing.T) *harness {
	t0 := time.Unix(1000, 0)
	h := &harness{t: t, t0: t0, now: t0, run: &fakeRun{}}
	h.link = &fakeLink{rx: udplink.NewReceiver(), quiet: t0.Add(udplink.StartSilence)}
	h.app = New(h.link, h.run, fakePower{h.run}, nil)
	h.app.clock = func() time.Time { return h.now }
	return h
}

// tick advances the clock one period and runs Step.
func (h *harness) tick() (protocol.Command, udplink.Tick) {
	h.t.Helper()
	h.now = h.now.Add(udplink.Period)
	cmd, tk := h.app.Step(h.now)
	if tk == udplink.Send {
		h.sent = append(h.sent, sentFrame{cmd, h.now})
	}
	return cmd, tk
}

// ticks runs n periods.
func (h *harness) ticks(n int) {
	for range n {
		h.tick()
	}
}

// last is the last frame sent.
func (h *harness) last() protocol.Command {
	h.t.Helper()
	if len(h.sent) == 0 {
		h.t.Fatal("no frame sent")
	}
	return h.sent[len(h.sent)-1].cmd
}

// tel delivers a telemetry frame at the current time with the next
// sequence number.
func (h *harness) tel(t protocol.Telemetry) {
	h.telSeq++
	b := protocol.EncodeTelemetry(t, h.telSeq)
	if ev, ok := h.link.rx.Datagram(b, h.now); ok {
		h.link.events = append(h.link.events, ev)
	}
}

// echo delivers telemetry echoing the current command with result,
// playing and pending.
func (h *harness) echo(result protocol.Result, playing, pending uint8) {
	h.tel(protocol.Telemetry{Counter: h.app.Command().Counter, Result: result, Playing: playing, Pending: pending})
}

// set changes the pad to c.
func (h *harness) set(c padmap.Controls) {
	h.pad = c
	h.app.SetControls(c, h.now)
}

func (h *harness) down(b padmap.Buttons) {
	h.set(padmap.Controls{Buttons: h.pad.Buttons | b, TriggerL: h.pad.TriggerL, TriggerR: h.pad.TriggerR})
}
func (h *harness) up(b padmap.Buttons) {
	h.set(padmap.Controls{Buttons: h.pad.Buttons &^ b, TriggerL: h.pad.TriggerL, TriggerR: h.pad.TriggerR})
}
func (h *harness) tap(b padmap.Buttons) { h.down(b); h.up(b) }

// goLive runs past the silence, sends a frame and delivers two telemetry
// frames echoing the baseline, so the link is live.
func (h *harness) goLive() {
	h.t.Helper()
	for {
		if _, tk := h.tick(); tk == udplink.Send {
			break
		}
	}
	for range 2 {
		h.tick()
		h.echo(protocol.ResultNone, 0, 0)
	}
	h.app.mu.Lock()
	h.app.pump(h.now)
	live := h.app.live(h.now)
	h.app.mu.Unlock()
	if !live {
		h.t.Fatal("the link is not live")
	}
}

// shownStatus is the controls panel's last request and status.
func (h *harness) shownStatus() (string, string) {
	sc := h.app.Screen(h.now)
	return sc.Controls.Last, sc.Controls.Status
}
