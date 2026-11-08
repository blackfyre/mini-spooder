package app

import (
	"fmt"
	"strings"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/motions"
	"github.com/blackfyre/mini-spooder/handheld/internal/padmap"
	"github.com/blackfyre/mini-spooder/handheld/internal/protocol"
	"github.com/blackfyre/mini-spooder/handheld/internal/wlan"
)

// GPIOs is the GPIO each servo index is attached to (design A9: the stock
// startup order).
var GPIOs = [8]int{15, 13, 12, 14, 2, 4, 5, 16}

// The signal levels the signal dot changes tone at, in dBm.
const (
	SignalGood = -67
	SignalWeak = -80
)

// Tone is a status colour's meaning; the screen picks the token.
type Tone int

const (
	ToneMuted  Tone = iota // no value
	ToneOK                 // bf-ok
	ToneWarn               // bf-warn
	ToneAccent             // bf-accent
)

// Screen is what the drive screen shows (design D8): every string and
// state, so cmd/minispooder only draws.
type Screen struct {
	State State
	// LinkChip is LINKED, STALE or NO TELEMETRY, with its tone.
	LinkChip     string
	LinkChipTone Tone
	// Signal is "-47 DBM" or "SIGNAL UNKNOWN", with its level's word and
	// tone.
	Signal      string
	SignalLevel string
	SignalTone  Tone

	Robot    Robot
	Controls ControlsView
	Link     LinkView
	Footer   []FooterKey
	// Leaving is shown while a leave waits, in the footer's place.
	Leaving string
	Notice  string
}

// Robot is the robot panel, from the newest telemetry shown.
type Robot struct {
	Have  bool // a telemetry frame has been shown
	Stale bool // none taken as newest for StaleAfter
	// Motion is the motion playing by name, or IDLE; Activity PLAYING or
	// IDLE.
	Motion   string
	Activity string
	// Pending is the motion pending, or NONE.
	Pending string
	Joints  [8]Joint
	// Stats is the robot's own link counts, as label and value.
	Stats [6]Stat
}

// Joint is one joint well: servo index, its GPIO and the angle (`---` for
// 255).
type Joint struct {
	Index int
	GPIO  int
	Angle string
}

// Stat is one labelled count.
type Stat struct {
	Label string
	Value string
}

// ControlsView is the controls panel.
type ControlsView struct {
	// Layer is BASE, R2, L2 or L2 + R2.
	Layer string
	Faces [3]Face
	// Last is the last request by motion name or STOP ("—" before any),
	// Status its status.
	Last       string
	Status     string
	StatusTone Tone
	Repeating  bool
}

// Face is one face button and what it requests in the active layer ("—"
// for nothing).
type Face struct {
	Key   string
	Label string
}

// LinkView is the link panel.
type LinkView struct {
	PerSecond  int
	RTT        string // "20 MS" or "UNKNOWN"
	Invalid    int
	Lost       int
	Late       int
	SendErrors int
	Sent       int
	// SinceLast is the time since the last valid frame ("—" before any).
	HaveFrame  bool
	SinceLast  time.Duration
	SinceLastS string
}

// FooterKey is one control named along the bottom.
type FooterKey struct {
	Key  string
	Does string
}

// Footer is the controls along the bottom.
var Footer = []FooterKey{{"B", "STOP"}, {"START", "STANDBY"}, {"MENU", "LEAVE"}}

// LeaveMessage is shown while a leave waits for Standby.
const LeaveMessage = "BRINGING THE ROBOT TO STANDBY · B STOPS"

func signalView(known bool, dbm int) (text, level string, tone Tone) {
	switch {
	case !known:
		return "SIGNAL UNKNOWN", "UNKNOWN", ToneMuted
	case dbm >= SignalGood:
		return fmt.Sprintf("%d DBM", dbm), "GOOD", ToneOK
	case dbm >= SignalWeak:
		return fmt.Sprintf("%d DBM", dbm), "FAIR", ToneWarn
	}
	return fmt.Sprintf("%d DBM", dbm), "WEAK", ToneAccent
}

func statusTone(s Status) Tone {
	switch s {
	case StatusTaken:
		return ToneOK
	case StatusRequested:
		return ToneMuted
	}
	return ToneWarn
}

// Screen is the screen model at now.
func (a *App) Screen(now time.Time) Screen {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.pump(now)
	st := a.link.Status(now)
	var sc Screen
	sc.State = a.state
	sc.Notice = a.notice
	sc.Signal, sc.SignalLevel, sc.SignalTone = signalView(a.signalKnown, a.signalDBm)

	stale := st.HaveNewest && now.Sub(st.NewestAt) >= StaleAfter
	switch {
	case !st.HaveNewest:
		sc.LinkChip, sc.LinkChipTone = "NO TELEMETRY", ToneMuted
	case stale:
		sc.LinkChip, sc.LinkChipTone = "STALE", ToneWarn
	default:
		sc.LinkChip, sc.LinkChipTone = "LINKED", ToneOK
	}

	sc.Robot = robotView(st.HaveNewest, stale, st.Newest)

	layer := padmap.LayerOf(a.controls)
	sc.Controls.Layer = layer.String()
	for i, f := range padmap.Faces(layer) {
		sc.Controls.Faces[i] = Face{Key: f.Key, Label: "—"}
		if f.Motion != 0 {
			sc.Controls.Faces[i].Label = motions.Name(f.Motion)
		}
	}
	sc.Controls.Last = "—"
	if a.shown.have {
		sc.Controls.Last = "STOP"
		if !a.shown.stop {
			sc.Controls.Last = motions.Label(a.shown.motion, "STOP")
		}
		s := StatusNotSent
		if !a.shown.notSent && a.newest != nil {
			s = a.newest.status
		}
		sc.Controls.Status, sc.Controls.StatusTone = s.String(), statusTone(s)
	}
	sc.Controls.Repeating = a.rep.active

	sc.Link = LinkView{PerSecond: st.PerSecond, RTT: "UNKNOWN", Invalid: st.Invalid, Lost: st.Lost, Late: st.Late,
		SendErrors: st.SendErrors, Sent: st.Sent, SinceLastS: "—"}
	if st.HaveNewest && st.RTTKnown {
		sc.Link.RTT = fmt.Sprintf("%d MS", st.RTT.Milliseconds())
	}
	if st.HaveValid {
		sc.Link.HaveFrame = true
		sc.Link.SinceLast = now.Sub(st.LastValidAt)
		sc.Link.SinceLastS = fmt.Sprintf("%d MS", sc.Link.SinceLast.Milliseconds())
	}
	sc.Footer = Footer
	if a.state == Leaving || a.state == Acting {
		sc.Leaving = LeaveMessage
	}
	return sc
}

func robotView(have, stale bool, t protocol.Telemetry) Robot {
	r := Robot{Have: have, Stale: stale, Motion: "—", Activity: "NO TELEMETRY", Pending: "—"}
	for i := range r.Joints {
		r.Joints[i] = Joint{Index: i, GPIO: GPIOs[i], Angle: "—"}
	}
	labels := [6]string{"ACCEPTED", "MALFORMED", "OUT OF SEQ", "OTHER SENDER", "RATE LIMITED", "FAILSAFES"}
	for i := range r.Stats {
		r.Stats[i] = Stat{Label: labels[i], Value: "—"}
	}
	if !have {
		return r
	}
	r.Motion = motions.Label(t.Playing, "IDLE")
	r.Activity = "IDLE"
	if t.Playing != 0 {
		r.Activity = "PLAYING"
	}
	r.Pending = motions.Label(t.Pending, "NONE")
	for i, v := range t.Angles {
		r.Joints[i].Angle = "---"
		if v != protocol.AngleNone {
			r.Joints[i].Angle = fmt.Sprintf("%d°", v)
		}
	}
	for i, v := range []uint16{t.Accepted, t.Malformed, t.OutOfSequence, t.OtherSender, t.RateLimited, t.Failsafes} {
		r.Stats[i].Value = fmt.Sprint(v)
	}
	return r
}

// Title is the drive screen's title, without its orange full stop.
const Title = "MINISPOODER"

// JoiningMessage is the join screen's text while the pak joins the robot's
// network.
const JoiningMessage = "Joining the robot's network " + wlan.RobotSSID + "..."

// JoinMessage is the join instructions for err, shown with retry and quit.
func JoinMessage(err error) string {
	return strings.TrimSpace(err.Error())
}

// JoinView is the join screen, drawn before the link starts: while the pak
// joins the robot's network (with its progress) and after a start that
// failed (with retry and quit). It has the drive screen's title and footer
// keycaps, and one panel.
type JoinView struct {
	// Status is the panel's kicker word, with its tone: JOINING, or NOT
	// JOINED after a failed start.
	Status     string
	StatusTone Tone
	Message    string
	// Progress is the share of the join done, from 0 to 1, shown when
	// ShowProgress is set.
	ShowProgress bool
	Progress     float64
	Footer       []FooterKey
}

// JoiningFooter is the join screen's controls while the pak joins.
var JoiningFooter = []FooterKey{{"MENU", "QUIT"}}

// JoinFailedFooter is the join screen's controls after a failed start: A
// (or Start) retries, Menu quits.
var JoinFailedFooter = []FooterKey{{"A", "RETRY"}, {"MENU", "QUIT"}}

// JoiningView is the join screen while the join runs, progress done (from 0
// to 1, clamped).
func JoiningView(progress float64) JoinView {
	return JoinView{Status: "JOINING", StatusTone: ToneMuted, Message: JoiningMessage,
		ShowProgress: true, Progress: min(max(progress, 0), 1), Footer: JoiningFooter}
}

// JoinFailedView is the join screen after a start that failed with err.
func JoinFailedView(err error) JoinView {
	return JoinView{Status: "NOT JOINED", StatusTone: ToneWarn, Message: JoinMessage(err), Footer: JoinFailedFooter}
}
