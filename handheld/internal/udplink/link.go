// Package udplink is the pak's UDP link to the robot (openspec change
// handheld-pak, design D4): one connected socket per run, kept across
// suspend and resume; at least 350 ms of silence before the first frame of
// a run and after a resume, during which whatever is read is discarded
// uncounted; then one command frame every 50 ms; and the receiver, which
// takes only exactly one valid telemetry frame per datagram and keeps the
// link-quality counts.
package udplink

import (
	"context"
	"errors"
	"net"
	"sync"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/protocol"
)

const (
	// DefaultAddr is the robot's address and drive port (design A1).
	DefaultAddr = "192.168.4.1:52041"
	// Period is drive-link's command period.
	Period = 50 * time.Millisecond
	// StartSilence is the silence before the first frame of a run and after
	// a resume: strictly more than the robot's 300 ms liveness, so a
	// previous session from the same address and port has ended and the
	// first frame starts a new one (drive-link's pre-start silence).
	StartSilence = 350 * time.Millisecond
	// maxEvents bounds the queue of received frames between two takes.
	maxEvents = 256
)

// Tick is what a step asks of the sender.
type Tick int

const (
	Send Tick = iota // send the command
	Skip             // send nothing this period (the silence)
	Stop             // end Run without sending
)

// Step returns the command for the period at now and what to do with it.
type Step func(now time.Time) (protocol.Command, Tick)

// Link is one run's socket, sender and receiver.
type Link struct {
	conn  *net.UDPConn
	raddr *net.UDPAddr
	now   func() time.Time
	write func([]byte) (int, error) // conn.Write; a test makes it fail
	done  chan struct{}

	mu     sync.Mutex
	quiet  time.Time // the silence lasts until then
	rx     *Receiver
	events []Event
	seq    uint8
}

// Dial makes the run's one socket, connected to addr, so the source port
// never changes and the kernel drops datagrams from any other address and
// port, and starts the receiver. local is the socket's own address, "" for
// any. The silence starts now.
func Dial(addr, local string) (*Link, error) {
	return dial(addr, local, time.Now)
}

func dial(addr, local string, now func() time.Time) (*Link, error) {
	raddr, err := net.ResolveUDPAddr("udp4", addr)
	if err != nil {
		return nil, err
	}
	var laddr *net.UDPAddr
	if local != "" {
		if laddr, err = net.ResolveUDPAddr("udp4", local); err != nil {
			return nil, err
		}
	}
	conn, err := net.DialUDP("udp4", laddr, raddr)
	if err != nil {
		return nil, err
	}
	l := &Link{conn: conn, raddr: raddr, now: now, write: conn.Write, done: make(chan struct{}), rx: NewReceiver()}
	l.quiet = now().Add(StartSilence)
	go l.receive()
	return l, nil
}

// LocalAddr is the socket's own address.
func (l *Link) LocalAddr() *net.UDPAddr { return l.conn.LocalAddr().(*net.UDPAddr) }

// Close closes the socket and waits for the receiver to stop.
func (l *Link) Close() error {
	err := l.conn.Close()
	<-l.done
	return err
}

func (l *Link) receive() {
	defer close(l.done)
	// One byte larger than the largest telemetry frame, so a datagram with a
	// byte too many is seen as such.
	buf := make([]byte, protocol.MaxFrameSize+1)
	for {
		n, from, err := l.conn.ReadFromUDP(buf)
		if errors.Is(err, net.ErrClosed) {
			return
		}
		if err != nil {
			continue // an ICMP error from an earlier send: the robot is not there yet
		}
		l.datagram(buf[:n], from, l.now())
	}
}

// datagram takes one datagram read at now.
func (l *Link) datagram(b []byte, from *net.UDPAddr, now time.Time) {
	l.mu.Lock()
	defer l.mu.Unlock()
	if now.Before(l.quiet) {
		return // read during the silence: discarded, not counted
	}
	// The connected socket already drops other senders; checked again for
	// the desktop build's sake. A foreign datagram is not counted.
	if from == nil || !from.IP.Equal(l.raddr.IP) || from.Port != l.raddr.Port {
		return
	}
	ev, ok := l.rx.Datagram(b, now)
	if !ok {
		return
	}
	if len(l.events) == maxEvents {
		copy(l.events, l.events[1:])
		l.events = l.events[:maxEvents-1]
	}
	l.events = append(l.events, ev)
}

// TakeEvents returns the frames received since the last call, oldest
// first.
func (l *Link) TakeEvents() []Event {
	l.mu.Lock()
	defer l.mu.Unlock()
	ev := l.events
	l.events = nil
	return ev
}

// Status is the receiver's state at now.
func (l *Link) Status(now time.Time) Status {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.rx.Status(now)
}

// Quiet tells whether the silence lasts at now.
func (l *Link) Quiet(now time.Time) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	return now.Before(l.quiet)
}

// Resume starts the silence again at now (after a suspend), rebaselines the
// sequence numbers and drops the frames not yet taken.
func (l *Link) Resume(now time.Time) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.quiet = now.Add(StartSilence)
	l.rx.Rebaseline()
	l.events = nil
}

// Run calls step every Period until ctx ends or step asks to stop, and
// sends the command when it asks to, the sequence number advancing by one
// per frame. Nothing is sent while the silence lasts. A send error is
// counted and the next period tries again. Periods missed while step blocks
// (a suspend) are dropped, not sent late in a burst.
func (l *Link) Run(ctx context.Context, step Step) error {
	ticker := time.NewTicker(Period)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return nil
		case <-ticker.C:
		}
		began := time.Now()
		now := l.now()
		cmd, tick := step(now)
		switch tick {
		case Stop:
			return nil
		case Send:
			if !l.Quiet(now) {
				l.send(cmd, now)
			}
		}
		if time.Since(began) >= Period {
			// The step blocked past a period (a suspend): start the periods
			// afresh, so the one it missed is not sent at once after it.
			ticker.Reset(Period)
		}
	}
}

// send sends one command frame.
func (l *Link) send(cmd protocol.Command, now time.Time) {
	l.mu.Lock()
	seq := l.seq
	l.seq++
	l.mu.Unlock()
	_, err := l.write(protocol.EncodeCommand(cmd, seq))
	l.mu.Lock()
	l.rx.Sent(seq, now, err)
	l.mu.Unlock()
}
