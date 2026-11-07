// Package e2e runs the pak's run loop (app.ParseFlags, app.Start, Run)
// against an in-process fake robot on a loopback port, with scripted pad
// input (openspec change handheld-pak, design D11).
package e2e

import (
	"context"
	"io"
	"net"
	"testing"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/app"
	"github.com/blackfyre/mini-spooder/handheld/internal/fakerobot"
	"github.com/blackfyre/mini-spooder/handheld/internal/motions"
	"github.com/blackfyre/mini-spooder/handheld/internal/padmap"
	"github.com/blackfyre/mini-spooder/handheld/internal/protocol"
)

// robot starts a fake robot on a free loopback port. Walking steps are
// shortened so the tests run quickly; Push up keeps its stock 7.18 s.
func robot(t *testing.T) *fakerobot.Robot {
	t.Helper()
	r, err := fakerobot.Listen("127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	d := map[uint8]time.Duration{}
	for k, v := range fakerobot.Durations {
		d[k] = v
	}
	for _, m := range []uint8{2, 3, 4, 5, 6, 7} {
		d[m] = 300 * time.Millisecond
	}
	d[motions.SayHi] = 600 * time.Millisecond
	r.SetDurations(d)
	ctx, cancel := context.WithCancel(context.Background())
	go func() { _ = r.Run(ctx) }()
	t.Cleanup(func() { cancel(); _ = r.Close() })
	return r
}

// pak is one run of the pak.
type pak struct {
	t     *testing.T
	s     *app.Session
	pad   padmap.Controls
	ended chan error
	start time.Time
}

func startPak(t *testing.T, r *fakerobot.Robot, local string) *pak {
	t.Helper()
	cfg, err := app.ParseFlags([]string{"--addr", r.Addr()}, io.Discard)
	if err != nil {
		t.Fatal(err)
	}
	p := &pak{t: t, ended: make(chan error, 1), start: time.Now()}
	p.s, err = app.Start(context.Background(), cfg, app.System{Local: local})
	if err != nil {
		t.Fatal(err)
	}
	go func() { p.ended <- p.s.Run(context.Background()) }()
	t.Cleanup(func() { _ = p.s.Close() })
	return p
}

func (p *pak) set(c padmap.Controls) {
	p.pad = c
	p.s.App.SetControls(c, time.Now())
}
func (p *pak) down(b padmap.Buttons) { p.set(padmap.Controls{Buttons: p.pad.Buttons | b}) }
func (p *pak) up(b padmap.Buttons)   { p.set(padmap.Controls{Buttons: p.pad.Buttons &^ b}) }
func (p *pak) tap(b padmap.Buttons)  { p.down(b); time.Sleep(10 * time.Millisecond); p.up(b) }

func (p *pak) screen() app.Screen { return p.s.App.Screen(time.Now()) }

// waitFor polls cond every 5 ms, failing after d.
func waitFor(t *testing.T, d time.Duration, what string, cond func() bool) {
	t.Helper()
	deadline := time.Now().Add(d)
	for !cond() {
		if time.Now().After(deadline) {
			t.Fatalf("timed out waiting for %s", what)
		}
		time.Sleep(5 * time.Millisecond)
	}
}

// live waits until a press would be taken: LINKED and two frames in.
func (p *pak) live() {
	p.t.Helper()
	waitFor(p.t, 2*time.Second, "a live link", func() bool {
		sc := p.screen()
		return sc.LinkChip == "LINKED" && sc.Link.PerSecond >= 3
	})
}

// requests is the robot's requests of motion m.
func requests(r *fakerobot.Robot, m uint8, stop bool) int {
	n := 0
	for _, q := range r.Snapshot().Requests {
		if q.Motion == m && (q.Motion == 0) == stop {
			n++
		}
	}
	return n
}

// exited waits for the run to end and returns when it ended.
func (p *pak) exited(within time.Duration) time.Time {
	p.t.Helper()
	select {
	case err := <-p.ended:
		if err != nil {
			p.t.Fatal(err)
		}
		return time.Now()
	case <-time.After(within):
		p.t.Fatal("the run did not end")
	}
	return time.Time{}
}

func TestDriveWalkStopLeave(t *testing.T) {
	r := robot(t)
	p := startPak(t, r, "")
	// No frame in the first 350 ms.
	waitFor(t, 2*time.Second, "the first frame", func() bool { return r.Snapshot().Datagrams > 0 })
	if d := r.Snapshot().FirstAt.Sub(p.start); d < 350*time.Millisecond {
		t.Fatalf("first frame %v after the start", d)
	}
	p.live()

	// A press plays a motion and is shown TAKEN.
	p.tap(padmap.A)
	waitFor(t, time.Second, "Say Hi taken", func() bool {
		sc := p.screen()
		return sc.Controls.Last == "SAY HI" && sc.Controls.Status == "TAKEN" && sc.Robot.Motion == "SAY HI"
	})
	waitFor(t, 2*time.Second, "Say Hi to end", func() bool { return p.screen().Robot.Motion == "IDLE" })

	// A held D-pad up repeats Forward until released.
	p.down(padmap.Up)
	waitFor(t, 4*time.Second, "three Forwards", func() bool { return requests(r, motions.Forward, false) >= 3 })
	if !p.screen().Controls.Repeating {
		t.Error("REPEATING not shown while walking")
	}
	p.up(padmap.Up)
	n := requests(r, motions.Forward, false)
	waitFor(t, 2*time.Second, "the walk to end", func() bool { return r.Snapshot().Playing == 0 })
	time.Sleep(300 * time.Millisecond)
	if m := requests(r, motions.Forward, false); m != n {
		t.Fatalf("%d Forwards requested after the release", m-n)
	}

	// B stops a motion.
	p.tap(padmap.R1)
	waitFor(t, time.Second, "Right shift playing", func() bool { return r.Snapshot().Playing == motions.RightSh })
	p.tap(padmap.B)
	waitFor(t, time.Second, "the stop", func() bool {
		sc := p.screen()
		return sc.Controls.Last == "STOP" && sc.Controls.Status == "TAKEN" && sc.Robot.Motion == "IDLE"
	})

	// Menu exits only after Standby is seen to finish and the frames have
	// stopped.
	press := time.Now()
	p.s.App.Press(app.ActionExit, press)
	end := p.exited(4 * time.Second)
	if end.Sub(press) >= app.LeaveTimeout {
		t.Fatalf("the leave took %v: timed out instead of seeing Standby finish", end.Sub(press))
	}
	snap := r.Snapshot()
	last := snap.Requests[len(snap.Requests)-1]
	if last.Motion != motions.Standby || last.Result != protocol.ResultAccepted || snap.Playing != 0 {
		t.Fatalf("the robot's last request %+v, playing %d", last, snap.Playing)
	}
	before := snap.Datagrams
	time.Sleep(200 * time.Millisecond)
	if after := r.Snapshot().Datagrams; after != before {
		t.Fatalf("%d frames after the exit", after-before)
	}
	if st := p.s.App.Screen(time.Now()).Link; st.Lost != 0 || st.Invalid != 0 {
		t.Errorf("lost %d invalid %d against the fake robot", st.Lost, st.Invalid)
	}
}

func TestStopDuringLeaveBehindPushUp(t *testing.T) {
	r := robot(t)
	p := startPak(t, r, "")
	p.live()
	p.tap(padmap.Y)
	waitFor(t, time.Second, "Push up playing", func() bool { return r.Snapshot().Playing == motions.PushUp })
	press := time.Now()
	p.s.App.Press(app.ActionExit, press)
	waitFor(t, time.Second, "Standby pending", func() bool { return r.Snapshot().Pending == motions.Standby })
	if p.screen().Leaving == "" {
		t.Error("no leave message during the wait")
	}
	p.tap(padmap.B)
	end := p.exited(4 * time.Second)
	if end.Sub(press) >= app.LeaveTimeout {
		t.Fatalf("the leave took %v: timed out", end.Sub(press))
	}
	var got []uint8
	for _, q := range r.Snapshot().Requests {
		got = append(got, q.Motion)
	}
	want := []uint8{motions.PushUp, motions.Standby, 0, motions.Standby}
	if len(got) != len(want) {
		t.Fatalf("requests %v, want %v", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("requests %v, want %v", got, want)
		}
	}
	if r.Snapshot().Playing != 0 {
		t.Fatal("the robot still plays after the leave")
	}
}

// A restart within 300 ms of the last frame, from the same source port,
// replays no request.
func TestQuickRestartReplaysNothing(t *testing.T) {
	r := robot(t)
	p := startPak(t, r, "")
	p.live()
	p.tap(padmap.L1)
	waitFor(t, time.Second, "Left shift", func() bool { return requests(r, motions.LeftSh, false) == 1 })
	port := p.s.Link.LocalAddr().Port
	p.s.App.Press(app.ActionExit, time.Now())
	p.exited(4 * time.Second)
	_ = p.s.Close()
	lastFrame := r.Snapshot().LastAt
	n := len(r.Snapshot().Requests)
	sessions := r.Snapshot().Sessions

	q := startPak(t, r, (&net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: port}).String())
	if time.Since(lastFrame) >= 300*time.Millisecond {
		t.Skip("the restart took 300 ms or more: nothing to check")
	}
	waitFor(t, 2*time.Second, "the restarted pak's first frame", func() bool { return r.Snapshot().LastAt.After(lastFrame) })
	if d := r.Snapshot().LastAt.Sub(lastFrame); d <= 300*time.Millisecond {
		t.Fatalf("the restarted pak's first frame %v after the last, want more than 300 ms", d)
	}
	q.live()
	time.Sleep(200 * time.Millisecond)
	snap := r.Snapshot()
	if len(snap.Requests) != n || snap.Sessions != sessions+1 {
		t.Fatalf("requests %d → %d, sessions %d → %d: want no request and a new session", n, len(snap.Requests), sessions, snap.Sessions)
	}
}

func TestSecondSenderIgnored(t *testing.T) {
	r := robot(t)
	p := startPak(t, r, "")
	p.live()
	ra, err := net.ResolveUDPAddr("udp4", r.Addr())
	if err != nil {
		t.Fatal(err)
	}
	other, err := net.DialUDP("udp4", nil, ra)
	if err != nil {
		t.Fatal(err)
	}
	defer other.Close()
	for i := range 10 {
		_, _ = other.Write(protocol.EncodeCommand(protocol.Command{Motion: motions.Dance1, Counter: uint8(i)}, uint8(i)))
		time.Sleep(20 * time.Millisecond)
	}
	snap := r.Snapshot()
	if len(snap.Requests) != 0 || snap.Counts[3] < 10 {
		t.Fatalf("requests %d, other-sender count %d", len(snap.Requests), snap.Counts[3])
	}
	if snap.Locked.Port() != uint16(p.s.Link.LocalAddr().Port) {
		t.Fatal("the robot is not locked to the pak")
	}
}

func TestCorruptedTelemetryCountedInvalid(t *testing.T) {
	r := robot(t)
	r.SetHooks(0, 10)
	p := startPak(t, r, "")
	p.live()
	waitFor(t, 5*time.Second, "three corrupted frames", func() bool { return r.Snapshot().Corrupted >= 3 })
	r.SetHooks(0, 0)
	time.Sleep(150 * time.Millisecond)
	inv := p.screen().Link.Invalid
	if c := r.Snapshot().Corrupted; inv != c {
		t.Fatalf("invalid %d, corrupted %d", inv, c)
	}
}
