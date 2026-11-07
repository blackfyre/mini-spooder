package udplink

import (
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/protocol"
)

const (
	// RebaselineAfter is the time without a frame taken as newest after
	// which the next valid frame is taken whatever its sequence number
	// (requirement Link quality).
	RebaselineAfter = 500 * time.Millisecond
	// RTTMaxAge is the oldest send time the round-trip time uses: an older
	// entry is a wrapped or stale sequence number.
	RTTMaxAge = time.Second
)

// Event is one valid telemetry frame received.
type Event struct {
	Seq       uint8
	Telemetry protocol.Telemetry
	At        time.Time
	// Late: not newer than the newest, so counted late and not taken.
	Late bool
	// Rebaseline: taken as newest whatever its sequence number (the first
	// of a run, after a resume or after RebaselineAfter).
	Rebaseline bool
}

// Quality is the link-quality counts since the start of the run.
type Quality struct {
	Invalid    int // datagrams from the robot's address that are not one valid telemetry frame
	Lost       int // gaps in the telemetry sequence numbers
	Late       int // telemetry not newer than the newest
	Sent       int
	SendErrors int
}

// Status is the receiver's state at one moment.
type Status struct {
	// The newest telemetry frame shown: taken as newest in sequence order.
	HaveNewest bool
	Newest     protocol.Telemetry
	NewestSeq  uint8
	NewestAt   time.Time
	// LastValidAt is the time of the last valid telemetry frame, late or
	// not.
	HaveValid   bool
	LastValidAt time.Time
	// PerSecond is the valid telemetry frames received in the last second.
	PerSecond int
	// RTT is the round-trip time from the newest frame shown, when known.
	RTT      time.Duration
	RTTKnown bool
	Quality
}

// Receiver validates the datagrams and keeps the sequence order, the
// counts and the send-time table. It is not safe for concurrent use; Link
// guards it.
type Receiver struct {
	st       Status
	rebase   bool
	sent     [256]time.Time
	received []time.Time // valid frames within the last second
}

// NewReceiver returns a receiver whose first valid frame rebaselines.
func NewReceiver() *Receiver { return &Receiver{rebase: true} }

// Rebaseline makes the next valid frame set the newest sequence number
// without counting anything lost or late.
func (r *Receiver) Rebaseline() { r.rebase = true }

// Sent records the send time of command frame seq, for the round-trip time.
func (r *Receiver) Sent(seq uint8, at time.Time, err error) {
	r.sent[seq] = at
	if err != nil {
		r.st.SendErrors++
	} else {
		r.st.Sent++
	}
}

// Invalid counts a datagram that is not one valid telemetry frame.
func (r *Receiver) Invalid() { r.st.Invalid++ }

// Datagram takes one datagram from the robot's address and port, received
// at now. It returns the event for a valid telemetry frame, and counts
// anything else invalid.
func (r *Receiver) Datagram(b []byte, now time.Time) (Event, bool) {
	f, ok := protocol.DecodeDatagram(b)
	if !ok || f.Type != protocol.TypeTelemetry {
		r.st.Invalid++
		return Event{}, false
	}
	r.st.HaveValid, r.st.LastValidAt = true, now
	r.received = append(r.received, now)
	ev := Event{Seq: f.Seq, Telemetry: f.Telemetry, At: now}
	switch {
	case r.rebase || !r.st.HaveNewest || now.Sub(r.st.NewestAt) >= RebaselineAfter:
		ev.Rebaseline = true
		r.rebase = false
	default:
		// The comparison is over the sequence type's width.
		d := f.Seq - r.st.NewestSeq
		const half = 1 << 7
		switch {
		case d == 0 || d >= half:
			r.st.Late++
			ev.Late = true
			return ev, true
		case d > 1:
			r.st.Lost += int(d) - 1
		}
	}
	r.st.HaveNewest = true
	r.st.Newest, r.st.NewestSeq, r.st.NewestAt = f.Telemetry, f.Seq, now
	r.st.RTT, r.st.RTTKnown = r.rtt(f.Telemetry, now)
	return ev, true
}

// rtt is the round-trip time from t received at now: the receive time minus
// the send time of the echoed command frame minus its echoed age.
func (r *Receiver) rtt(t protocol.Telemetry, now time.Time) (time.Duration, bool) {
	if t.AgeMs == protocol.AgeUnknown {
		return 0, false
	}
	sent := r.sent[t.CommandSeq]
	if sent.IsZero() || now.Sub(sent) > RTTMaxAge {
		return 0, false
	}
	d := now.Sub(sent) - time.Duration(t.AgeMs)*time.Millisecond
	if d < 0 {
		return 0, false
	}
	return d, true
}

// Status is the receiver's state at now.
func (r *Receiver) Status(now time.Time) Status {
	i := 0
	for i < len(r.received) && now.Sub(r.received[i]) >= time.Second {
		i++
	}
	r.received = append(r.received[:0], r.received[i:]...)
	st := r.st
	st.PerSecond = len(r.received)
	return st
}
