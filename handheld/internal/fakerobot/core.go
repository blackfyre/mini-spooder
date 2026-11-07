// Package fakerobot is a stand-in robot for the pak's tests and desktop runs
// (openspec change handheld-pak, design D10, requirement Fake robot). It
// follows drive-link's contract towards one remote: the sender lock, the
// 300 ms liveness, the sequence window, the counter baseline, one request
// per counter change with motion 0 as stop, and the echo of the last
// processed counter value, its result and the newest accepted command frame
// in telemetry every 100 ms. It stands in for the motions with a fixed time
// per motion and motion-playback's single pending request; it models no
// gait and reports fixed joint angles. It uses the pak's own codec.
package fakerobot

import (
	"net/netip"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/motions"
	"github.com/blackfyre/mini-spooder/handheld/internal/protocol"
)

const (
	// Liveness is drive-link's session liveness.
	Liveness = 300 * time.Millisecond
	// TelemetryPeriod is drive-link's telemetry period.
	TelemetryPeriod = 100 * time.Millisecond
)

// Durations is each motion's playing time: the stock tables' durations
// (motion-playback design), not the keyframes.
var Durations = map[uint8]time.Duration{
	1: 400 * time.Millisecond,
	2: 1650 * time.Millisecond, 3: 1650 * time.Millisecond, 4: 1650 * time.Millisecond, 5: 1650 * time.Millisecond,
	6: 1200 * time.Millisecond, 7: 1200 * time.Millisecond,
	8:  500 * time.Millisecond,
	9:  3200 * time.Millisecond,
	10: 4070 * time.Millisecond,
	11: 7180 * time.Millisecond,
	12: 1140 * time.Millisecond,
	13: 3000 * time.Millisecond, 14: 2700 * time.Millisecond, 15: 3000 * time.Millisecond,
	99: 380 * time.Millisecond, 100: 380 * time.Millisecond,
}

// Rest is the fixed joint angles reported while output is on: the stock
// page's defaults.
var Rest = [8]uint8{70, 90, 90, 110, 110, 90, 90, 70}

// Request is one request the fake robot made of its motion player.
type Request struct {
	Motion  uint8 // 0 for a stop
	Counter uint8
	At      time.Time
	Result  protocol.Result
}

// Datagram is one telemetry datagram to send.
type Datagram struct {
	To netip.AddrPort
	B  []byte
}

// Core is the fake robot's logic with an injected clock: Handle each
// datagram as it is read, Tick every few milliseconds. It is not safe for
// concurrent use; Robot guards it.
type Core struct {
	// The session.
	live     bool
	locked   netip.AddrPort
	lastAt   time.Time // when the newest accepted frame was read
	lastSeq  uint8
	counter  uint8           // the last accepted frame's counter
	echo     uint8           // the last processed counter, or the baseline
	result   protocol.Result // its result
	nextTel  time.Time
	telSeq   uint8
	outputOn bool

	// The motion player.
	playing   uint8
	playUntil time.Time
	pending   uint8

	counts   [6]uint16 // accepted, malformed, out of sequence, other sender, rate limited, failsafes
	requests []Request
	sessions int
	telSent  int

	// Every datagram read, and the read times of the first and the last.
	datagrams             int
	firstAt, lastDatagram time.Time

	// Durations is the motions' playing times (the package's Durations
	// unless a test sets others).
	Durations map[uint8]time.Duration

	// Test hooks: drop or corrupt every nth telemetry frame (0: none).
	DropEvery, CorruptEvery int
	// Corrupted and Dropped count what the hooks did.
	Corrupted, Dropped int
}

// NewCore returns an idle robot with output off since boot.
func NewCore() *Core { return &Core{Durations: Durations} }

// endSession is the link-loss failsafe: unlock, stop, safe state.
func (c *Core) endSession() {
	c.live = false
	c.counts[5]++
	c.stop()
	c.outputOn = false
}

func (c *Core) stop() {
	c.playing, c.pending = 0, 0
}

// lapse ends a session whose newest accepted frame is too old.
func (c *Core) lapse(now time.Time) {
	if c.live && now.Sub(c.lastAt) > Liveness {
		c.endSession()
	}
}

// Handle takes one datagram from from, read at now.
func (c *Core) Handle(b []byte, from netip.AddrPort, now time.Time) {
	if c.datagrams == 0 {
		c.firstAt = now
	}
	c.datagrams++
	c.lastDatagram = now
	c.lapse(now)
	if c.live && from != c.locked {
		c.counts[3]++
		return
	}
	f, ok := protocol.DecodeDatagram(b)
	if !ok || f.Type != protocol.TypeCommand {
		c.counts[1]++
		return
	}
	if !c.live {
		// A new session: the first frame sets the baseline only.
		c.live, c.locked = true, from
		c.sessions++
		c.lastAt, c.lastSeq = now, f.Seq
		c.counter, c.echo, c.result = f.Command.Counter, f.Command.Counter, protocol.ResultNone
		c.counts[0]++
		c.stop() // the session start calls motion playback's stop
		c.nextTel = now
		return
	}
	d := f.Seq - c.lastSeq
	switch {
	case d == 0 || d >= 128:
		c.counts[2]++
		return
	case d >= 9:
		c.counts[2]++
		c.endSession()
		return
	}
	c.counts[0]++
	c.lastAt, c.lastSeq = now, f.Seq
	if f.Command.Counter == c.counter {
		return
	}
	c.counter = f.Command.Counter
	c.request(f.Command, now)
}

// request is one request taken from the frame carrying a counter change.
func (c *Core) request(cmd protocol.Command, now time.Time) {
	r := Request{Motion: cmd.Motion, Counter: cmd.Counter, At: now}
	switch {
	case cmd.Motion == motions.Stop:
		c.stop()
		r.Result = protocol.ResultStop
	case c.Durations[cmd.Motion] == 0:
		r.Result = protocol.ResultRejected
	default:
		r.Result = protocol.ResultAccepted
		// The next explicit motion request ends the safe state.
		c.outputOn = true
		if c.playing != 0 {
			c.pending = cmd.Motion // latest wins
		} else {
			c.playing, c.playUntil = cmd.Motion, now.Add(c.Durations[cmd.Motion])
		}
	}
	c.echo, c.result = cmd.Counter, r.Result
	c.requests = append(c.requests, r)
}

// Tick advances the motion player and the lapse to now, and returns the
// telemetry due.
func (c *Core) Tick(now time.Time) []Datagram {
	c.lapse(now)
	for c.playing != 0 && !now.Before(c.playUntil) {
		end := c.playUntil
		c.playing = 0
		if c.pending != 0 {
			c.playing, c.playUntil, c.pending = c.pending, end.Add(c.Durations[c.pending]), 0
		}
	}
	if !c.live || now.Before(c.nextTel) {
		return nil
	}
	if now.Sub(c.nextTel) > TelemetryPeriod {
		c.nextTel = now.Add(TelemetryPeriod)
	} else {
		c.nextTel = c.nextTel.Add(TelemetryPeriod)
	}
	return c.telemetry(now)
}

func (c *Core) telemetry(now time.Time) []Datagram {
	age := now.Sub(c.lastAt) / time.Millisecond
	if age > 254 {
		age = protocol.AgeUnknown
	}
	t := protocol.Telemetry{
		Playing: c.playing, Pending: c.pending, Counter: c.echo, Result: c.result,
		CommandSeq: c.lastSeq, AgeMs: uint8(age),
		Accepted: c.counts[0], Malformed: c.counts[1], OutOfSequence: c.counts[2],
		OtherSender: c.counts[3], RateLimited: c.counts[4], Failsafes: c.counts[5],
	}
	for i := range t.Angles {
		t.Angles[i] = protocol.AngleNone
		if c.outputOn {
			t.Angles[i] = Rest[i]
		}
	}
	b := protocol.EncodeTelemetry(t, c.telSeq)
	c.telSeq++
	c.telSent++
	if c.DropEvery > 0 && c.telSent%c.DropEvery == 0 {
		c.Dropped++
		return nil
	}
	if c.CorruptEvery > 0 && c.telSent%c.CorruptEvery == 0 {
		b[len(b)-1] ^= 0xFF
		c.Corrupted++
	}
	return []Datagram{{To: c.locked, B: b}}
}

// Snapshot is the fake robot's state for the tests.
type Snapshot struct {
	Live     bool
	Locked   netip.AddrPort
	Playing  uint8
	Pending  uint8
	Requests []Request
	Sessions int
	// Counts: accepted, malformed, out of sequence, other sender, rate
	// limited, failsafes.
	Counts    [6]uint16
	OutputOn  bool
	Corrupted int
	// Datagrams is every datagram read, FirstAt and LastAt the read times
	// of the first and the last.
	Datagrams       int
	FirstAt, LastAt time.Time
}

// Snapshot is the state now.
func (c *Core) Snapshot() Snapshot {
	return Snapshot{Live: c.live, Locked: c.locked, Playing: c.playing, Pending: c.pending,
		Requests: append([]Request(nil), c.requests...), Sessions: c.sessions, Counts: c.counts,
		OutputOn: c.outputOn, Corrupted: c.Corrupted, Datagrams: c.datagrams, FirstAt: c.firstAt, LastAt: c.lastDatagram}
}
