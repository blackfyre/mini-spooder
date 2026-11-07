package udplink

import (
	"bytes"
	"context"
	"errors"
	"net"
	"sync"
	"testing"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/protocol"
)

// listener is a stand-in robot socket on loopback that records what it
// receives.
type listener struct {
	conn *net.UDPConn
	mu   sync.Mutex
	got  []received
}

type received struct {
	b    []byte
	from *net.UDPAddr
	at   time.Time
}

func listen(t *testing.T) *listener {
	t.Helper()
	conn, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	l := &listener{conn: conn}
	go func() {
		buf := make([]byte, 64)
		for {
			n, from, err := conn.ReadFromUDP(buf)
			if err != nil {
				return
			}
			l.mu.Lock()
			l.got = append(l.got, received{bytes.Clone(buf[:n]), from, time.Now()})
			l.mu.Unlock()
		}
	}()
	t.Cleanup(func() { conn.Close() })
	return l
}

func (l *listener) addr() string { return l.conn.LocalAddr().String() }

func (l *listener) frames() []received {
	l.mu.Lock()
	defer l.mu.Unlock()
	return append([]received(nil), l.got...)
}

func always(cmd protocol.Command) Step {
	return func(time.Time) (protocol.Command, Tick) { return cmd, Send }
}

// The silence, with an injected clock: quiet until 350 ms after the dial and
// after a resume.
func TestSilenceInjectedClock(t *testing.T) {
	t0 := time.Unix(1000, 0)
	now := t0
	l, err := dial("127.0.0.1:9", "", func() time.Time { return now })
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	if !l.Quiet(t0.Add(349 * time.Millisecond)) {
		t.Error("not quiet 349 ms after the start")
	}
	if l.Quiet(t0.Add(StartSilence)) {
		t.Error("still quiet 350 ms after the start")
	}
	r := t0.Add(10 * time.Second)
	l.Resume(r)
	if !l.Quiet(r.Add(349*time.Millisecond)) || l.Quiet(r.Add(StartSilence)) {
		t.Error("the resume's silence is not 350 ms")
	}
}

// On loopback: no datagram for 350 ms, then one command frame every 50 ms,
// all from one source port, the sequence rising by one.
func TestSilenceRateSequencePort(t *testing.T) {
	rob := listen(t)
	start := time.Now()
	l, err := Dial(rob.addr(), "")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	ctx, cancel := context.WithTimeout(context.Background(), StartSilence+1100*time.Millisecond)
	defer cancel()
	_ = l.Run(ctx, always(protocol.Command{}))
	got := rob.frames()
	if len(got) == 0 {
		t.Fatal("nothing received")
	}
	if first := got[0].at.Sub(start); first < StartSilence {
		t.Errorf("first frame %v after the start, want at least %v", first, StartSilence)
	}
	// About 1 s of frames after the first.
	var in []received
	for _, r := range got {
		if r.at.Sub(got[0].at) <= time.Second {
			in = append(in, r)
		}
	}
	if len(in) < 18 || len(in) > 22 {
		t.Errorf("%d frames in the second after the first, want about 20", len(in))
	}
	for i, r := range got {
		f, ok := protocol.DecodeDatagram(r.b)
		if !ok || f.Type != protocol.TypeCommand || len(r.b) != protocol.CommandFrameSize {
			t.Fatalf("datagram %d is not one command frame: %x", i, r.b)
		}
		if f.Seq != uint8(i) {
			t.Fatalf("datagram %d has sequence %d", i, f.Seq)
		}
		if r.from.Port != got[0].from.Port {
			t.Fatalf("datagram %d from port %d, the first from %d", i, r.from.Port, got[0].from.Port)
		}
		if i > 0 {
			if gap := r.at.Sub(got[i-1].at); gap < Period/2 || gap > 2*Period {
				t.Errorf("gap %v before datagram %d, want about %v", gap, i, Period)
			}
		}
	}
}

func TestSendErrorCountedAndNextSent(t *testing.T) {
	rob := listen(t)
	var offset time.Duration
	l, err := dial(rob.addr(), "", func() time.Time { return time.Now().Add(offset) })
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	offset = time.Hour // past the silence
	n := 0
	real := l.write
	l.write = func(b []byte) (int, error) {
		n++
		if n == 2 {
			return 0, errors.New("network down")
		}
		return real(b)
	}
	calls := 0
	_ = l.Run(context.Background(), func(time.Time) (protocol.Command, Tick) {
		calls++
		if calls > 4 {
			return protocol.Command{}, Stop
		}
		return protocol.Command{}, Send
	})
	st := l.Status(time.Now())
	if st.SendErrors != 1 || st.Sent != 3 {
		t.Fatalf("sent %d, errors %d; want 3 and 1", st.Sent, st.SendErrors)
	}
	time.Sleep(20 * time.Millisecond)
	if got := len(rob.frames()); got != 3 {
		t.Fatalf("robot received %d frames, want 3 (the one after the error too)", got)
	}
}

func telem(t protocol.Telemetry, seq uint8) []byte { return protocol.EncodeTelemetry(t, seq) }

func TestReceivedFrames(t *testing.T) {
	r := NewReceiver()
	now := time.Unix(100, 0)
	tl := protocol.Telemetry{Playing: 14, Pending: 2, Counter: 3, Result: protocol.ResultAccepted}
	if _, ok := r.Datagram(telem(tl, 10), now); !ok {
		t.Fatal("valid telemetry not taken")
	}
	shown := r.Status(now)
	if !shown.HaveNewest || shown.Newest != tl {
		t.Fatalf("status %+v", shown)
	}
	good := telem(protocol.Telemetry{Playing: 9}, 11)
	badCRC := bytes.Clone(good)
	badCRC[len(badCRC)-1] ^= 1
	for name, b := range map[string][]byte{
		"bad CRC":        badCRC,
		"one extra byte": append(bytes.Clone(good), 0),
		"two frames":     append(bytes.Clone(good), good...),
		"command frame":  protocol.EncodeCommand(protocol.Command{Motion: 2}, 11),
	} {
		before := r.Status(now)
		if _, ok := r.Datagram(b, now); ok {
			t.Errorf("%s taken", name)
		}
		after := r.Status(now)
		if after.Invalid != before.Invalid+1 {
			t.Errorf("%s: invalid %d → %d", name, before.Invalid, after.Invalid)
		}
		after.Invalid = before.Invalid
		if after != before {
			t.Errorf("%s changed the status beyond the invalid count", name)
		}
	}
}

// A datagram from another address or port is dropped and changes nothing;
// one read during the silence is discarded uncounted.
func TestForeignAndSilenceDiscarded(t *testing.T) {
	now := time.Unix(100, 0)
	clock := now
	l, err := dial("127.0.0.1:52041", "", func() time.Time { return clock })
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	robot := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 52041}
	b := telem(protocol.Telemetry{Playing: 2}, 1)
	l.datagram(b, robot, now.Add(100*time.Millisecond)) // in the silence
	l.datagram(telem(protocol.Telemetry{}, 1)[:5], robot, now.Add(100*time.Millisecond))
	after := now.Add(StartSilence)
	l.datagram(b, &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 52042}, after)
	l.datagram(b, &net.UDPAddr{IP: net.IPv4(127, 0, 0, 2), Port: 52041}, after)
	st := l.Status(after)
	if st.HaveNewest || st.HaveValid || st.Invalid != 0 || len(l.TakeEvents()) != 0 {
		t.Fatalf("status %+v after the silence's and foreign datagrams", st)
	}
	l.datagram(b, robot, after)
	if st := l.Status(after); !st.HaveNewest || st.Newest.Playing != 2 {
		t.Fatalf("the robot's frame after the silence not taken: %+v", st)
	}
}

func feed(r *Receiver, start time.Time, step time.Duration, seqs ...uint8) time.Time {
	at := start
	for _, s := range seqs {
		r.Datagram(telem(protocol.Telemetry{Counter: s}, s), at)
		at = at.Add(step)
	}
	return at
}

func TestLinkQuality(t *testing.T) {
	t0 := time.Unix(100, 0)
	step := 100 * time.Millisecond

	r := NewReceiver()
	feed(r, t0, step, 10, 11, 14)
	if st := r.Status(t0); st.Lost != 2 || st.Late != 0 {
		t.Errorf("10, 11, 14: lost %d late %d, want 2 and 0", st.Lost, st.Late)
	}

	r = NewReceiver()
	at := feed(r, t0, step, 14)
	ev, _ := r.Datagram(telem(protocol.Telemetry{Counter: 12}, 12), at)
	if st := r.Status(at); st.Late != 1 || st.NewestSeq != 14 || !ev.Late {
		t.Errorf("12 after 14: late %d, newest %d", st.Late, st.NewestSeq)
	}

	r = NewReceiver()
	at = feed(r, t0, step, 10)
	ev, _ = r.Datagram(telem(protocol.Telemetry{Counter: 10}, 10), at)
	if st := r.Status(at); st.Late != 1 || !ev.Late {
		t.Errorf("a repeated sequence number: late %d", st.Late)
	}

	r = NewReceiver()
	feed(r, t0, step, 254, 255, 0, 1)
	if st := r.Status(t0); st.Lost != 0 || st.Late != 0 || st.NewestSeq != 1 {
		t.Errorf("wrap: %+v", st.Quality)
	}

	// A gap of 500 ms: any sequence number is taken, nothing counted.
	r = NewReceiver()
	at = feed(r, t0, step, 10)
	ev, _ = r.Datagram(telem(protocol.Telemetry{}, 3), at.Add(RebaselineAfter))
	if st := r.Status(at); st.Lost != 0 || st.Late != 0 || st.NewestSeq != 3 || !ev.Rebaseline {
		t.Errorf("after a gap: %+v newest %d", st.Quality, st.NewestSeq)
	}

	// A resume: likewise.
	r = NewReceiver()
	at = feed(r, t0, step, 100)
	r.Rebaseline()
	feed(r, at, step, 7)
	if st := r.Status(at); st.Lost != 0 || st.Late != 0 || st.NewestSeq != 7 {
		t.Errorf("after a resume: %+v newest %d", st.Quality, st.NewestSeq)
	}
}

// Telemetry 100 shown, then the robot restarts and sends 0, 1, 2 … every
// 100 ms: those are late until 500 ms after frame 100 was taken, and the
// first after that is taken without being counted late.
func TestRobotRestartWhileFramesArrive(t *testing.T) {
	t0 := time.Unix(100, 0)
	r := NewReceiver()
	r.Datagram(telem(protocol.Telemetry{Counter: 100}, 100), t0)
	for i := range 10 {
		at := t0.Add(time.Duration(i+1) * 80 * time.Millisecond)
		ev, ok := r.Datagram(telem(protocol.Telemetry{Counter: uint8(i)}, uint8(i)), at)
		st := r.Status(at)
		if at.Sub(t0) < RebaselineAfter {
			if !ok || !ev.Late || st.NewestSeq != 100 {
				t.Fatalf("frame %d at %v: late %v, newest %d", i, at.Sub(t0), ev.Late, st.NewestSeq)
			}
		} else {
			if ev.Late || !ev.Rebaseline || st.NewestSeq != uint8(i) {
				t.Fatalf("frame %d at %v: late %v rebaseline %v newest %d", i, at.Sub(t0), ev.Late, ev.Rebaseline, st.NewestSeq)
			}
			if st.Late != i {
				t.Fatalf("late count %d, want %d", st.Late, i)
			}
			return
		}
	}
	t.Fatal("no frame taken after 500 ms")
}

func TestRoundTripTime(t *testing.T) {
	T := time.Unix(100, 0)
	r := NewReceiver()
	r.Sent(40, T, nil)
	r.Datagram(telem(protocol.Telemetry{CommandSeq: 40, AgeMs: 10}, 1), T.Add(30*time.Millisecond))
	if st := r.Status(T); !st.RTTKnown || st.RTT != 20*time.Millisecond {
		t.Fatalf("RTT %v known %v, want 20 ms", st.RTT, st.RTTKnown)
	}
	for name, c := range map[string]struct {
		age   uint8
		after time.Duration
	}{
		"age 255":         {protocol.AgeUnknown, 30 * time.Millisecond},
		"sent over 1 s":   {10, 1001 * time.Millisecond},
		"negative result": {50, 30 * time.Millisecond},
	} {
		r := NewReceiver()
		r.Sent(40, T, nil)
		r.Datagram(telem(protocol.Telemetry{CommandSeq: 40, AgeMs: c.age}, 1), T.Add(c.after))
		if st := r.Status(T); st.RTTKnown {
			t.Errorf("%s: RTT %v known, want unknown", name, st.RTT)
		}
	}
	// Never sent.
	r = NewReceiver()
	r.Datagram(telem(protocol.Telemetry{CommandSeq: 40, AgeMs: 1}, 1), T)
	if r.Status(T).RTTKnown {
		t.Error("RTT known for a sequence never sent")
	}
}

func TestPerSecond(t *testing.T) {
	t0 := time.Unix(100, 0)
	r := NewReceiver()
	feed(r, t0, 100*time.Millisecond, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)
	if n := r.Status(t0.Add(1150 * time.Millisecond)).PerSecond; n != 10 {
		t.Fatalf("%d frames in the last second, want 10", n)
	}
}
