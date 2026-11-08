package app

import (
	"errors"
	"testing"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/padmap"
	"github.com/blackfyre/mini-spooder/handheld/internal/protocol"
)

func TestScreenMotionByName(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.tel(protocol.Telemetry{Playing: 14, Pending: 2})
	sc := h.app.Screen(h.now)
	if sc.Robot.Motion != "DANCE 2" || sc.Robot.Pending != "FORWARD" || sc.Robot.Activity != "PLAYING" {
		t.Fatalf("robot %+v", sc.Robot)
	}
	h.tel(protocol.Telemetry{Playing: 99})
	if sc := h.app.Screen(h.now); sc.Robot.Motion != "CENTRE" || sc.Robot.Pending != "NONE" {
		t.Fatalf("robot %+v, want CENTRE and NONE", sc.Robot)
	}
	h.tel(protocol.Telemetry{Playing: 0})
	if sc := h.app.Screen(h.now); sc.Robot.Motion != "IDLE" || sc.Robot.Activity != "IDLE" {
		t.Fatalf("robot %+v, want IDLE", sc.Robot)
	}
	h.tel(protocol.Telemetry{Playing: 42})
	if sc := h.app.Screen(h.now); sc.Robot.Motion != "MOTION 42" {
		t.Fatalf("robot %+v, want MOTION 42", sc.Robot)
	}
}

func TestScreenJoints(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.tel(protocol.Telemetry{Angles: [8]uint8{70, 90, 90, 110, 255, 90, 90, 70}, Malformed: 3})
	sc := h.app.Screen(h.now)
	want := [8]int{15, 13, 12, 14, 2, 4, 5, 16}
	for i, j := range sc.Robot.Joints {
		if j.Index != i || j.GPIO != want[i] {
			t.Errorf("well %d: index %d GPIO %d, want GPIO %d", i, j.Index, j.GPIO, want[i])
		}
	}
	if sc.Robot.Joints[4].Angle != "---" || sc.Robot.Joints[0].Angle != "70°" {
		t.Errorf("angles %+v", sc.Robot.Joints)
	}
	if sc.Robot.Stats[1].Label != "MALFORMED" || sc.Robot.Stats[1].Value != "3" {
		t.Errorf("stats %+v", sc.Robot.Stats)
	}
}

func TestScreenRequestTakenAndLost(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.app.mu.Lock()
	h.app.cmd.Counter = 4
	h.app.mu.Unlock()
	h.tap(padmap.Up)
	h.tick()
	h.tel(protocol.Telemetry{Counter: 5, Result: accepted, Playing: 2})
	sc := h.app.Screen(h.now)
	if sc.Controls.Last != "FORWARD" || sc.Controls.Status != "TAKEN" || sc.Controls.StatusTone != ToneOK {
		t.Fatalf("controls %+v", sc.Controls)
	}
}

func TestScreenLayer(t *testing.T) {
	h := newHarness(t)
	h.set(padmap.Controls{TriggerL: 32767})
	sc := h.app.Screen(h.now)
	got := [3]string{sc.Controls.Faces[0].Label, sc.Controls.Faces[1].Label, sc.Controls.Faces[2].Label}
	if sc.Controls.Layer != "L2" || got != [3]string{"Lie", "Sleep", "Zero position"} {
		t.Fatalf("layer %q faces %v", sc.Controls.Layer, got)
	}
	h.set(padmap.Controls{})
	sc = h.app.Screen(h.now)
	got = [3]string{sc.Controls.Faces[0].Label, sc.Controls.Faces[1].Label, sc.Controls.Faces[2].Label}
	if sc.Controls.Layer != "BASE" || got != [3]string{"Say Hi", "Fighting", "Push up"} {
		t.Fatalf("layer %q faces %v", sc.Controls.Layer, got)
	}
	h.set(padmap.Controls{TriggerL: 32767, TriggerR: 32767})
	if sc := h.app.Screen(h.now); sc.Controls.Faces[0].Label != "—" {
		t.Fatalf("both triggers: faces %+v", sc.Controls.Faces)
	}
}

func TestScreenStale(t *testing.T) {
	h := newHarness(t)
	if sc := h.app.Screen(h.now); sc.LinkChip != "NO TELEMETRY" || sc.Robot.Have {
		t.Fatalf("chip %q before telemetry", sc.LinkChip)
	}
	h.goLive()
	h.tel(protocol.Telemetry{Playing: 2})
	if sc := h.app.Screen(h.now); sc.LinkChip != "LINKED" || sc.Robot.Stale {
		t.Fatalf("chip %q", sc.LinkChip)
	}
	h.now = h.now.Add(StaleAfter)
	sc := h.app.Screen(h.now)
	// The word, not only a colour.
	if sc.LinkChip != "STALE" || !sc.Robot.Stale || sc.Robot.Motion != "FORWARD" {
		t.Fatalf("after 500 ms: chip %q robot %+v, want STALE with the last values", sc.LinkChip, sc.Robot)
	}
	a := sc.Link.SinceLast
	h.now = h.now.Add(time.Second)
	if b := h.app.Screen(h.now).Link.SinceLast; b <= a {
		t.Fatalf("time since the last frame %v then %v", a, b)
	}
}

func TestScreenRTTAndSignal(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.tel(protocol.Telemetry{CommandSeq: 0, AgeMs: 255})
	if sc := h.app.Screen(h.now); sc.Link.RTT != "UNKNOWN" {
		t.Fatalf("RTT %q with age 255", sc.Link.RTT)
	}
	if sc := h.app.Screen(h.now); sc.Signal != "SIGNAL UNKNOWN" || sc.SignalTone != ToneMuted {
		t.Fatalf("signal %q off the device", sc.Signal)
	}
	for _, c := range []struct {
		dbm  int
		tone Tone
	}{{-40, ToneOK}, {-67, ToneOK}, {-68, ToneWarn}, {-80, ToneWarn}, {-81, ToneAccent}} {
		text, level, tone := signalView(true, c.dbm)
		if tone != c.tone || level == "" || text == "" {
			t.Errorf("%d dBm: %q %q %v, want tone %v", c.dbm, text, level, tone, c.tone)
		}
	}
}

func TestScreenFooterAndLeave(t *testing.T) {
	h := newHarness(t)
	sc := h.app.Screen(h.now)
	if len(sc.Footer) != 3 || sc.Footer[0] != (FooterKey{"B", "STOP"}) || sc.Leaving != "" {
		t.Fatalf("footer %+v leaving %q", sc.Footer, sc.Leaving)
	}
}

func TestJoinViews(t *testing.T) {
	for _, c := range []struct {
		in, want float64
	}{{-0.5, 0}, {0.25, 0.25}, {1.5, 1}} {
		v := JoiningView(c.in)
		if !v.ShowProgress || v.Progress != c.want || v.Status != "JOINING" || v.Message != JoiningMessage {
			t.Fatalf("JoiningView(%v) = %+v, want progress %v", c.in, v, c.want)
		}
		if len(v.Footer) != 1 || v.Footer[0] != (FooterKey{"MENU", "QUIT"}) {
			t.Fatalf("joining footer %+v", v.Footer)
		}
	}
	v := JoinFailedView(errors.New("  no robot\n"))
	if v.ShowProgress || v.Status != "NOT JOINED" || v.StatusTone != ToneWarn || v.Message != "no robot" {
		t.Fatalf("JoinFailedView = %+v", v)
	}
	if len(v.Footer) != 2 || v.Footer[0].Key != "A" || v.Footer[1].Key != "MENU" {
		t.Fatalf("join failed footer %+v", v.Footer)
	}
}
