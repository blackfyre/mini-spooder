package fakerobot

import (
	"net/netip"
	"testing"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/protocol"
)

var (
	pak   = netip.MustParseAddrPort("127.0.0.1:40000")
	other = netip.MustParseAddrPort("127.0.0.1:40001")
)

// sender is a remote: it sends frames with rising sequence numbers.
type sender struct {
	c    *Core
	from netip.AddrPort
	seq  uint8
}

func (s *sender) send(m, counter uint8, at time.Time) {
	s.c.Handle(protocol.EncodeCommand(protocol.Command{Motion: m, Counter: counter}, s.seq), s.from, at)
	s.seq++
}

// lastTel ticks to at and decodes the telemetry sent, if any.
func lastTel(t *testing.T, c *Core, at time.Time) (protocol.Telemetry, bool) {
	t.Helper()
	out := c.Tick(at)
	if len(out) == 0 {
		return protocol.Telemetry{}, false
	}
	f, ok := protocol.DecodeDatagram(out[len(out)-1].B)
	if !ok {
		t.Fatal("telemetry does not decode")
	}
	return f.Telemetry, true
}

func TestPlaysAndStops(t *testing.T) {
	c := NewCore()
	s := &sender{c: c, from: pak}
	t0 := time.Unix(10, 0)
	s.send(0, 0, t0) // baseline
	s.send(2, 1, t0.Add(50*time.Millisecond))
	tel, ok := lastTel(t, c, t0.Add(60*time.Millisecond))
	if !ok || tel.Playing != 2 || tel.Result != protocol.ResultAccepted || tel.Counter != 1 || tel.CommandSeq != 1 {
		t.Fatalf("after Forward: %+v", tel)
	}
	if tel.Angles != Rest {
		t.Fatalf("angles %v, want %v", tel.Angles, Rest)
	}
	s.send(0, 2, t0.Add(100*time.Millisecond))
	tel, _ = lastTel(t, c, t0.Add(160*time.Millisecond))
	if tel.Playing != 0 || tel.Result != protocol.ResultStop || tel.Counter != 2 {
		t.Fatalf("after stop: %+v", tel)
	}
}

func TestPendingRequest(t *testing.T) {
	c := NewCore()
	s := &sender{c: c, from: pak}
	t0 := time.Unix(10, 0)
	s.send(0, 0, t0)
	s.send(2, 1, t0)
	s.send(4, 2, t0.Add(50*time.Millisecond))
	tel, _ := lastTel(t, c, t0.Add(60*time.Millisecond))
	if tel.Playing != 2 || tel.Pending != 4 {
		t.Fatalf("%+v, want 2 playing and 4 pending", tel)
	}
	// Frames keep the session live while Forward plays out.
	for at := t0.Add(100 * time.Millisecond); at.Before(t0.Add(Durations[2] + 50*time.Millisecond)); at = at.Add(50 * time.Millisecond) {
		s.send(4, 2, at)
	}
	snap := c.Snapshot()
	c.Tick(t0.Add(Durations[2] + 10*time.Millisecond))
	if snap = c.Snapshot(); snap.Playing != 4 || snap.Pending != 0 {
		t.Fatalf("after Forward's time: playing %d pending %d", snap.Playing, snap.Pending)
	}
}

func TestRestartedRemoteWithinLiveness(t *testing.T) {
	c := NewCore()
	s := &sender{c: c, from: pak}
	t0 := time.Unix(10, 0)
	s.send(0, 0, t0)
	s.send(2, 1, t0.Add(50*time.Millisecond))
	// The remote restarts: same address and port, 200 ms later, a sequence
	// 20 ahead and a changed counter.
	s.seq += 20
	s.send(9, 7, t0.Add(250*time.Millisecond))
	snap := c.Snapshot()
	if snap.Live || snap.Counts[5] != 1 || len(snap.Requests) != 1 {
		t.Fatalf("after the jump: live %v failsafes %d requests %d", snap.Live, snap.Counts[5], len(snap.Requests))
	}
	s.send(9, 8, t0.Add(300*time.Millisecond))
	snap = c.Snapshot()
	if !snap.Live || snap.Sessions != 2 || len(snap.Requests) != 1 {
		t.Fatalf("the next frame: live %v sessions %d requests %d, want a new session and no request", snap.Live, snap.Sessions, len(snap.Requests))
	}
}

func TestOldFrameKeepsSession(t *testing.T) {
	for _, back := range []uint8{0, 128, 200, 255} {
		c := NewCore()
		s := &sender{c: c, from: pak}
		t0 := time.Unix(10, 0)
		s.send(0, 0, t0)
		s.send(0, 0, t0)
		last := s.seq - 1
		s.seq = last + back
		s.send(2, 1, t0.Add(10*time.Millisecond))
		snap := c.Snapshot()
		if !snap.Live || len(snap.Requests) != 0 || snap.Counts[2] != 1 {
			t.Errorf("d=%d: live %v requests %d out of sequence %d", back, snap.Live, len(snap.Requests), snap.Counts[2])
		}
	}
}

func TestSecondRemoteIgnored(t *testing.T) {
	c := NewCore()
	a := &sender{c: c, from: pak}
	b := &sender{c: c, from: other}
	t0 := time.Unix(10, 0)
	for i := range 10 {
		at := t0.Add(time.Duration(i) * 50 * time.Millisecond)
		a.send(0, 0, at)
		b.send(9, uint8(i), at)
		for _, d := range c.Tick(at) {
			if d.To != pak {
				t.Fatalf("telemetry to %v", d.To)
			}
		}
	}
	snap := c.Snapshot()
	if snap.Locked != pak || len(snap.Requests) != 0 || snap.Counts[3] != 10 {
		t.Fatalf("locked %v requests %d other %d", snap.Locked, len(snap.Requests), snap.Counts[3])
	}
}

func TestLinkLossToIdleAndUnlock(t *testing.T) {
	c := NewCore()
	s := &sender{c: c, from: pak}
	t0 := time.Unix(10, 0)
	s.send(0, 0, t0)
	s.send(11, 1, t0)
	c.Tick(t0.Add(301 * time.Millisecond))
	snap := c.Snapshot()
	if snap.Live || snap.Playing != 0 || snap.OutputOn || snap.Counts[5] != 1 {
		t.Fatalf("after 301 ms: %+v", snap)
	}
	// Another station takes over; its first frame is a baseline.
	b := &sender{c: c, from: other}
	b.send(9, 5, t0.Add(400*time.Millisecond))
	tel, _ := lastTel(t, c, t0.Add(400*time.Millisecond))
	if snap := c.Snapshot(); snap.Locked != other || len(snap.Requests) != 1 {
		t.Fatalf("handover: %+v", snap)
	}
	if tel.Angles[0] != protocol.AngleNone || tel.Result != protocol.ResultNone || tel.Counter != 5 {
		t.Fatalf("telemetry after link loss: %+v, want limp angles and the baseline", tel)
	}
}

func TestTelemetryScheduleAndHooks(t *testing.T) {
	c := NewCore()
	c.CorruptEvery = 10
	s := &sender{c: c, from: pak}
	t0 := time.Unix(10, 0)
	var frames, bad int
	for at := t0; at.Before(t0.Add(3 * time.Second)); at = at.Add(10 * time.Millisecond) {
		if at.Sub(t0)%(50*time.Millisecond) == 0 {
			s.send(0, 0, at)
		}
		for _, d := range c.Tick(at) {
			frames++
			if _, ok := protocol.DecodeDatagram(d.B); !ok {
				bad++
			}
		}
	}
	if frames < 29 || frames > 31 || bad != frames/10 {
		t.Fatalf("%d frames in 3 s, %d corrupted", frames, bad)
	}
}
