package app

import (
	"testing"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/padmap"
	"github.com/blackfyre/mini-spooder/handheld/internal/protocol"
	"github.com/blackfyre/mini-spooder/handheld/internal/udplink"
)

// untilStop runs periods until Step stops, at most n, and returns the time
// of the last frame sent.
func (h *harness) untilStop(n int, each func()) time.Time {
	h.t.Helper()
	for range n {
		if _, tk := h.tick(); tk == udplink.Stop {
			return h.sent[len(h.sent)-1].at
		}
		if each != nil {
			each()
		}
	}
	h.t.Fatalf("still sending after %d periods", n)
	return time.Time{}
}

func (h *harness) noFrameAfterStop() {
	h.t.Helper()
	n := len(h.sent)
	for range 5 {
		if _, tk := h.tick(); tk != udplink.Stop {
			h.t.Fatal("a step after the wait did not stop")
		}
	}
	if len(h.sent) != n {
		h.t.Fatal("a frame was sent after the wait")
	}
}

func TestLeaveWhileIdle(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.app.Press(ActionExit, h.now)
	press := h.now
	if sc := h.app.Screen(h.now); sc.Leaving != LeaveMessage {
		t.Fatalf("leave message %q", sc.Leaving)
	}
	h.tick()
	if h.last() != cmd(1, 1) {
		t.Fatalf("frame %+v, want Standby with counter 1", h.last())
	}
	// A frame echoing an earlier counter reporting idle, and one echoing the
	// leave's counter before Standby is seen, end nothing.
	h.tel(protocol.Telemetry{Counter: 0, Result: none})
	h.tick()
	h.echo(accepted, 0, 0)
	h.tick()
	if h.app.State() != Leaving {
		t.Fatal("the wait ended before Standby was seen")
	}
	h.echo(accepted, 1, 0)
	h.tick()
	h.echo(accepted, 0, 0)
	last := h.untilStop(3, nil)
	if last.Sub(press) >= LeaveTimeout {
		t.Fatal("the wait timed out instead of ending on Standby")
	}
	h.noFrameAfterStop()
	if h.app.Ended() != ActionExit {
		t.Fatalf("ended %v", h.app.Ended())
	}
}

func TestLeaveTimesOut(t *testing.T) {
	for name, echo := range map[string]func(h *harness){
		"push up playing": func(h *harness) { h.echo(accepted, 11, 1) },
		"none yet":        func(h *harness) { h.echo(none, 0, 0) },
		"rejected":        func(h *harness) { h.echo(rejected, 0, 0) },
		"no telemetry":    func(*harness) {},
	} {
		h := newHarness(t)
		h.goLive()
		h.app.Press(ActionExit, h.now)
		press := h.now
		last := h.untilStop(100, func() { echo(h) })
		if d := last.Sub(press); d < LeaveTimeout-udplink.Period || d > LeaveTimeout {
			t.Errorf("%s: last frame %v after the press, want 3 s", name, d)
		}
		h.noFrameAfterStop()
	}
}

func TestLeaveRightAfterStop(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.tap(padmap.B)
	h.app.Press(ActionExit, h.now)
	h.tick()
	if h.last() != cmd(0, 1) {
		t.Fatalf("frame %+v, want the stop", h.last())
	}
	h.ticks(3)
	if h.last() != cmd(0, 1) {
		t.Fatalf("frame %+v before the stop's echo, want the stop", h.last())
	}
	h.echo(stopRes, 0, 0)
	h.tick()
	if h.last() != cmd(1, 2) {
		t.Fatalf("frame %+v, want Standby with counter 2", h.last())
	}
}

// The leave replaces an unsent motion request.
func TestLeaveReplacesUnsentRequest(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.tap(padmap.A)
	h.app.Press(ActionExit, h.now)
	h.tick()
	if h.last() != cmd(1, 1) {
		t.Fatalf("frame %+v, want Standby with counter 1", h.last())
	}
}

func TestControlsDuringTheWait(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.down(padmap.Up) // a walking control held into the leave
	h.tick()
	h.echo(accepted, 2, 0)
	h.app.Press(ActionExit, h.now)
	h.tick()
	c := h.last()
	h.echo(accepted, 2, 0)
	for _, b := range []padmap.Buttons{padmap.A, padmap.Start, padmap.Down, padmap.L1} {
		h.tap(b)
		h.tick()
	}
	h.echo(accepted, 0, 0) // Forward ended: no repetition
	h.tick()
	if h.last() != c || c != cmd(1, 2) {
		t.Fatalf("frames %+v then %+v, want Standby unchanged", c, h.last())
	}
	// A second Menu or Power press does not start another leave.
	h.app.Press(ActionPowerOff, h.now)
	h.echo(accepted, 1, 0)
	h.tick()
	h.echo(accepted, 0, 0)
	h.untilStop(3, nil)
	if h.app.Ended() != ActionExit {
		t.Fatalf("ended %v, want exit", h.app.Ended())
	}
}

func TestStopDuringTheWait(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.tap(padmap.Y)
	h.tick()
	h.echo(accepted, 11, 0)
	h.app.Press(ActionExit, h.now)
	press := h.now
	h.tick()
	h.echo(accepted, 11, 1) // Push up plays, Standby pending
	h.tap(padmap.B)
	h.tick()
	if h.last() != cmd(0, 3) {
		t.Fatalf("frame %+v, want the stop with counter 3", h.last())
	}
	// A frame built before the stop arrived, reporting the earlier Standby
	// as ended, does not end the wait.
	h.tel(protocol.Telemetry{Counter: 2, Result: accepted, Playing: 0})
	h.tick()
	if h.app.State() != Leaving {
		t.Fatal("the wait ended on a frame from before the stop")
	}
	h.echo(stopRes, 0, 0)
	h.tick()
	if h.last() != cmd(1, 4) {
		t.Fatalf("frame %+v, want Standby again with counter 4", h.last())
	}
	h.echo(accepted, 1, 0)
	h.tick()
	h.echo(accepted, 0, 0)
	last := h.untilStop(3, nil)
	if last.Sub(press) >= LeaveTimeout {
		t.Fatal("timed out instead of ending on the new Standby")
	}
	h.noFrameAfterStop()
}

func TestStopLateInTheWait(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.app.Press(ActionExit, h.now)
	press := h.now
	echo := func() { h.echo(accepted, 11, 1) }
	for h.now.Sub(press) < LeaveTimeout-200*time.Millisecond {
		h.tick()
		echo()
	}
	h.tap(padmap.B)
	last := h.untilStop(20, func() { h.echo(stopRes, 0, 0) })
	if d := last.Sub(press); d > LeaveTimeout {
		t.Fatalf("last frame %v after the press: the B press extended the wait", d)
	}
}

func TestStopDuringTheWaitWithoutALiveLink(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.app.Press(ActionExit, h.now)
	h.ticks(7) // no telemetry: not live
	h.tap(padmap.B)
	if last, st := h.shownStatus(); last != "STOP" || st != "NOT SENT" {
		t.Fatalf("shown %q %q, want STOP NOT SENT", last, st)
	}
	h.tick()
	if h.last() != cmd(1, 1) {
		t.Fatalf("frame %+v, want Standby unchanged", h.last())
	}
}

// A late frame (older by its sequence number than the one shown) does not
// end the wait, even when it reports Standby ended.
func TestLateFrameDoesNotEndTheWait(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.app.Press(ActionExit, h.now)
	h.tick()
	h.echo(accepted, 1, 0) // seen playing
	seen := h.telSeq
	h.tick()
	h.echo(accepted, 1, 0)
	h.telSeq = seen - 2 // the next frame is late
	h.echo(accepted, 0, 0)
	h.telSeq = seen + 1
	h.tick()
	if h.app.State() != Leaving {
		t.Fatal("a late frame ended the wait")
	}
}

// B taken after Standby was seen to finish but before the next period: the
// stop is still sent and Standby requested again before the exit.
func TestStopAfterStandbyFinishedBeforeTick(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.app.Press(ActionExit, h.now)
	h.tick()
	h.echo(accepted, 1, 0)
	h.tick()
	h.echo(accepted, 0, 0)
	h.tap(padmap.B) // pumps the frame that ends the wait, then takes B
	h.tick()
	if h.last() != cmd(0, 2) {
		t.Fatalf("frame %+v, want the stop sent", h.last())
	}
	h.echo(stopRes, 0, 0)
	h.tick()
	if h.last() != cmd(1, 3) {
		t.Fatalf("frame %+v, want Standby again", h.last())
	}
}
