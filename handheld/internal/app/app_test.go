package app

import (
	"slices"
	"testing"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/padmap"
	"github.com/blackfyre/mini-spooder/handheld/internal/protocol"
	"github.com/blackfyre/mini-spooder/handheld/internal/udplink"
)

const (
	accepted = protocol.ResultAccepted
	stopRes  = protocol.ResultStop
	none     = protocol.ResultNone
	rejected = protocol.ResultRejected
)

func cmd(m, c uint8) protocol.Command { return protocol.Command{Motion: m, Counter: c} }

func TestSilenceThenZeroState(t *testing.T) {
	h := newHarness(t)
	for {
		_, tk := h.tick()
		if tk == udplink.Send {
			break
		}
		if h.now.Sub(h.t0) >= udplink.StartSilence {
			t.Fatalf("no frame at %v", h.now.Sub(h.t0))
		}
	}
	if d := h.sent[0].at.Sub(h.t0); d < udplink.StartSilence {
		t.Fatalf("first frame %v after the start", d)
	}
	h.ticks(20)
	for _, f := range h.sent {
		if f.cmd != cmd(0, 0) {
			t.Fatalf("frame %+v with nothing pressed, want motion 0 counter 0", f.cmd)
		}
	}
}

func TestPressBeforeTheRobotAnswers(t *testing.T) {
	h := newHarness(t)
	h.ticks(8) // past the silence, frames going out, no telemetry
	h.tap(padmap.Up)
	if last, st := h.shownStatus(); last != "FORWARD" || st != "NOT SENT" {
		t.Fatalf("shown %q %q, want FORWARD NOT SENT", last, st)
	}
	// One telemetry frame is not enough.
	h.tick()
	h.echo(none, 0, 0)
	h.tap(padmap.A)
	h.ticks(3)
	h.echo(none, 0, 0)
	h.ticks(3)
	for _, f := range h.sent {
		if f.cmd != cmd(0, 0) {
			t.Fatalf("frame %+v, want motion 0 counter 0", f.cmd)
		}
	}
}

// Telemetry that arrived before the first frame (in the silence: the link
// discards it, so here it is simply before the first frame) does not count
// towards the gate.
func TestGateCountsOnlyAfterFirstFrame(t *testing.T) {
	h := newHarness(t)
	h.now = h.now.Add(100 * time.Millisecond)
	h.echo(none, 0, 0)
	h.echo(none, 0, 0)
	h.ticks(7) // the first frame goes out
	h.tick()
	h.echo(none, 0, 0)
	h.tap(padmap.A)
	if _, st := h.shownStatus(); st != "NOT SENT" {
		t.Fatalf("press with one frame after the start: %q, want NOT SENT", st)
	}
	h.tick()
	h.echo(none, 0, 0)
	h.tap(padmap.A)
	h.tick()
	if h.last() != cmd(9, 1) {
		t.Fatalf("frame %+v, want Say Hi with counter 1", h.last())
	}
}

func TestOneRequestPerPress(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.down(padmap.A)
	h.tick()
	if h.last() != cmd(9, 1) {
		t.Fatalf("frame %+v, want motion 9 counter 1", h.last())
	}
	for range 200 { // 10 s held
		h.tick()
		h.echo(accepted, 9, 0)
	}
	h.up(padmap.A)
	h.ticks(3)
	if h.last() != cmd(9, 1) {
		t.Fatalf("frame %+v after the hold and release, want motion 9 counter 1", h.last())
	}
}

func TestLayerFromTrigger(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.set(padmap.Controls{TriggerR: 32767})
	h.down(padmap.A)
	h.tick()
	if h.last() != cmd(13, 1) {
		t.Fatalf("frame %+v, want Dance 1", h.last())
	}
}

func TestStopAfterAMotionRequest(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.tap(padmap.Up)
	h.tick()
	h.echo(accepted, 2, 0)
	h.tap(padmap.B)
	h.tick()
	var got []protocol.Command
	for _, f := range h.sent {
		if len(got) == 0 || got[len(got)-1] != f.cmd {
			got = append(got, f.cmd)
		}
	}
	want := []protocol.Command{cmd(0, 0), cmd(2, 1), cmd(0, 2)}
	if !slices.Equal(got, want) {
		t.Fatalf("frames %v, want %v", got, want)
	}
}

func TestStandbyRightAfterStop(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.tap(padmap.Up)
	h.tick()
	h.echo(accepted, 2, 0)
	h.tap(padmap.B)
	h.tick()
	h.tap(padmap.Start)
	if last, st := h.shownStatus(); last != "STANDBY" || st != "NOT SENT" {
		t.Fatalf("shown %q %q, want STANDBY NOT SENT", last, st)
	}
	h.ticks(3)
	h.tel(protocol.Telemetry{Counter: 1, Result: accepted, Playing: 2}) // before the stop arrived
	h.ticks(2)
	if h.last() != cmd(0, 2) {
		t.Fatalf("frame %+v, want the stop", h.last())
	}
	h.tap(padmap.Start)
	h.tick()
	if h.last() != cmd(0, 2) {
		t.Fatalf("Start before the stop's echo: %+v", h.last())
	}
	h.echo(stopRes, 0, 0)
	h.tap(padmap.Start)
	h.tick()
	if h.last() != cmd(1, 3) {
		t.Fatalf("Start after the stop's echo: %+v, want Standby counter 3", h.last())
	}
}

func TestStopAndStandbyInOnePeriod(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.tap(padmap.B)
	h.tap(padmap.Start)
	h.tick()
	if h.last() != cmd(0, 1) {
		t.Fatalf("frame %+v, want the stop with counter 1", h.last())
	}
	if last, st := h.shownStatus(); last != "STANDBY" || st != "NOT SENT" {
		t.Fatalf("shown %q %q", last, st)
	}
}

func TestTwoPressesInOnePeriod(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.tap(padmap.Up)
	h.tap(padmap.A)
	h.tick()
	if h.last() != cmd(2, 1) {
		t.Fatalf("frame %+v, want Forward with counter 1", h.last())
	}
	if last, st := h.shownStatus(); last != "SAY HI" || st != "NOT SENT" {
		t.Fatalf("shown %q %q, want SAY HI NOT SENT", last, st)
	}
}

// A stop in the same period replaces the unsent motion request: the frame
// carries one new request, the stop.
func TestStopReplacesUnsentRequest(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.tap(padmap.Up)
	h.tap(padmap.B)
	h.tick()
	if h.last() != cmd(0, 1) {
		t.Fatalf("frame %+v, want the stop with counter 1", h.last())
	}
	for _, f := range h.sent {
		if f.cmd.Motion == 2 {
			t.Fatal("Forward was sent")
		}
	}
}

func TestCounterWraps(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.app.mu.Lock()
	h.app.cmd.Counter = 255
	h.app.mu.Unlock()
	h.tap(padmap.B)
	h.tick()
	if h.last() != cmd(0, 0) {
		t.Fatalf("frame %+v, want stop with counter 0", h.last())
	}
}

func TestStopAlwaysTakenOnALiveLink(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.tap(padmap.Y)
	h.tick()
	h.echo(accepted, 11, 0)
	h.set(padmap.Controls{TriggerL: 32767, TriggerR: 32767})
	h.down(padmap.B)
	h.tick()
	if h.last() != cmd(0, 2) {
		t.Fatalf("frame %+v, want the stop", h.last())
	}
	// A second stop, before the first's echo, is taken too.
	h.up(padmap.B)
	h.down(padmap.B)
	h.tick()
	if h.last() != cmd(0, 3) {
		t.Fatalf("frame %+v, want the second stop", h.last())
	}
}

func TestRequestStatus(t *testing.T) {
	for _, c := range []struct {
		result protocol.Result
		want   string
	}{{accepted, "TAKEN"}, {none, "NOT TAKEN"}, {rejected, "REJECTED"}} {
		h := newHarness(t)
		h.goLive()
		h.app.mu.Lock()
		h.app.cmd.Counter = 4
		h.app.mu.Unlock()
		h.tap(padmap.Up)
		h.tick()
		if last, st := h.shownStatus(); last != "FORWARD" || st != "REQUESTED" {
			t.Fatalf("shown %q %q, want FORWARD REQUESTED", last, st)
		}
		h.tel(protocol.Telemetry{Counter: 5, Result: c.result})
		if last, st := h.shownStatus(); last != "FORWARD" || st != c.want {
			t.Errorf("echo with %v: %q %q, want FORWARD %s", c.result, last, st, c.want)
		}
	}
}

func TestNotConfirmed(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.tap(padmap.B)
	h.tick()
	for range 18 { // 950 ms after the press, telemetry echoing an older counter
		h.tick()
		h.tel(protocol.Telemetry{Counter: 0})
	}
	if _, st := h.shownStatus(); st != "REQUESTED" {
		t.Fatalf("%q before 1 s", st)
	}
	h.tick()
	h.tel(protocol.Telemetry{Counter: 0})
	if _, st := h.shownStatus(); st != "NOT CONFIRMED" {
		t.Fatalf("%q after 1 s, want NOT CONFIRMED", st)
	}
	// A stop marked not confirmed releases motion requests.
	h.tap(padmap.Start)
	h.tick()
	if h.last() != cmd(1, 2) {
		t.Fatalf("frame %+v, want Standby after the unconfirmed stop", h.last())
	}
}

// walk holds D-pad up and plays Forward to its end in telemetry once.
func (h *harness) playToEnd(motion uint8) {
	h.tick()
	h.echo(accepted, motion, 0)
	h.tick()
	h.echo(accepted, motion, 0)
	h.tick()
	h.echo(accepted, 0, 0)
}

func TestWalkingWhileHeld(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.down(padmap.Up)
	h.playToEnd(2)
	h.tick()
	if h.last() != cmd(2, 2) {
		t.Fatalf("frame %+v, want Forward again with counter 2", h.last())
	}
	if !h.app.Screen(h.now).Controls.Repeating {
		t.Error("REPEATING not shown")
	}
	h.playToEnd(2)
	h.tick()
	if h.last() != cmd(2, 3) {
		t.Fatalf("frame %+v, want Forward again with counter 3", h.last())
	}
	// At most one repetition per ended request.
	for range 4 {
		h.tick()
		h.echo(accepted, 0, 0)
	}
	if h.last() != cmd(2, 3) {
		t.Fatalf("frame %+v, want no repetition before the new one is seen", h.last())
	}
	// Released while it plays: nothing more.
	h.tick()
	h.echo(accepted, 2, 0)
	h.up(padmap.Up)
	h.playToEnd(2)
	h.ticks(2)
	if h.last() != cmd(2, 3) {
		t.Fatalf("frame %+v after the release, want no change", h.last())
	}
}

// The ended request is the one echoed: a frame from before the request
// (an older counter) reporting idle does not end it.
func TestRepeatNeedsTheRequestsEcho(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.down(padmap.Up)
	h.tick()
	h.tel(protocol.Telemetry{Counter: 0, Result: none})
	h.tel(protocol.Telemetry{Counter: 0, Result: accepted, Playing: 2})
	h.tel(protocol.Telemetry{Counter: 0, Result: accepted})
	h.tick()
	if h.last() != cmd(2, 1) {
		t.Fatalf("frame %+v, want only the press's request", h.last())
	}
}

func TestNotSeenPlaying(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.down(padmap.Up)
	for range 10 {
		h.tick()
		h.echo(accepted, 0, 0)
	}
	if h.last() != cmd(2, 1) {
		t.Fatalf("frame %+v, want no repetition", h.last())
	}
}

func TestStopWhileWalking(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.down(padmap.Up)
	h.playToEnd(2)
	h.tick()
	h.echo(accepted, 2, 0)
	h.down(padmap.B)
	h.tick()
	h.echo(stopRes, 0, 0)
	for range 6 {
		h.tick()
		h.echo(stopRes, 0, 0)
	}
	if h.last() != cmd(0, 3) {
		t.Fatalf("frame %+v, want the stop and no further Forward", h.last())
	}
}

func TestAnotherWalkingControlWhileHeld(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.down(padmap.Up)
	h.playToEnd(2)
	h.tick()
	h.echo(accepted, 2, 0)
	h.down(padmap.Left)
	h.tick()
	if h.last() != cmd(6, 3) {
		t.Fatalf("frame %+v, want Turn left once", h.last())
	}
	h.playToEnd(6)
	h.tick()
	if h.last() != cmd(6, 4) {
		t.Fatalf("frame %+v, want Turn left repeated", h.last())
	}
}

func TestLinkLostWhileWalking(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.down(padmap.Up)
	h.tick()
	h.echo(accepted, 2, 0)
	h.ticks(6) // 300 ms without telemetry: not live
	h.tap(padmap.A)
	if _, st := h.shownStatus(); st != "NOT SENT" {
		t.Fatalf("press after the loss: %q, want NOT SENT", st)
	}
	h.tick()
	h.echo(accepted, 2, 0)
	h.tick()
	h.echo(accepted, 2, 0)
	h.tick()
	h.echo(accepted, 0, 0)
	h.ticks(2)
	if h.last() != cmd(2, 1) {
		t.Fatalf("frame %+v, want no Forward after the loss", h.last())
	}
	// Live again (two frames after the first frame since the loss): a new
	// press walks again.
	h.up(padmap.Up)
	h.down(padmap.Up)
	h.tick()
	if h.last() != cmd(2, 2) {
		t.Fatalf("frame %+v, want Forward on the new press", h.last())
	}
}

func TestPressAfterResumeAndPowerSave(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.app.Press(ActionSuspend, h.now)
	h.tick()
	if h.last() != cmd(1, 1) {
		t.Fatalf("frame %+v, want Standby", h.last())
	}
	h.echo(accepted, 1, 0)
	h.tick()
	h.echo(accepted, 0, 0)
	// The wait ends: the next step runs the suspend and resumes.
	h.run.block = func(string) { h.now = h.now.Add(10 * time.Second) }
	sentBefore := len(h.sent)
	_, tk := h.tick()
	if tk == udplink.Send || len(h.sent) != sentBefore {
		t.Fatal("a frame was sent in the step that suspended")
	}
	want := []string{"power restore", SuspendScript, "power off"}
	if !slices.Equal(h.run.calls, want) {
		t.Fatalf("calls %v, want %v", h.run.calls, want)
	}
	if h.app.State() != Driving || len(h.link.resumes) != 1 {
		t.Fatalf("state %v, resumes %d", h.app.State(), len(h.link.resumes))
	}
	resumed := h.link.resumes[0]
	// The silence after the resume, then frames again, the gate closed.
	for {
		_, tk := h.tick()
		if tk == udplink.Send {
			break
		}
	}
	if d := h.sent[len(h.sent)-1].at.Sub(resumed); d < udplink.StartSilence {
		t.Fatalf("first frame %v after the resume", d)
	}
	h.tap(padmap.A)
	h.tick()
	h.echo(accepted, 0, 0)
	h.tap(padmap.A)
	if h.last() != cmd(1, 1) {
		t.Fatalf("frame %+v, want no request before two telemetry frames", h.last())
	}
	h.tick()
	h.echo(accepted, 0, 0)
	h.tap(padmap.A)
	h.tick()
	if h.last() != cmd(9, 2) {
		t.Fatalf("frame %+v, want Say Hi once the link is live", h.last())
	}
}

func TestPowerOffRestoresFirst(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.app.Press(ActionPowerOff, h.now)
	h.ticks(61)
	if h.app.State() != Done || h.app.Ended() != ActionPowerOff {
		t.Fatalf("state %v ended %v", h.app.State(), h.app.Ended())
	}
	want := []string{"power restore", PowerOffCommand}
	if !slices.Equal(h.run.calls, want) {
		t.Fatalf("calls %v, want %v", h.run.calls, want)
	}
}

// A release with a completion frame queued requests no further step.
func TestReleaseWithQueuedEnd(t *testing.T) {
	h := newHarness(t)
	h.goLive()
	h.down(padmap.Up)
	h.tick()
	h.echo(accepted, 2, 0)
	h.tick()
	h.echo(accepted, 0, 0) // queued: not yet pumped
	h.up(padmap.Up)
	h.ticks(2)
	if h.last() != cmd(2, 1) {
		t.Fatalf("frame %+v, want no Forward after the release", h.last())
	}
}
