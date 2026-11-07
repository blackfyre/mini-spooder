package fakerobot

import (
	"context"
	"errors"
	"net"
	"net/netip"
	"sync"
	"time"
)

// tickPeriod is how often the robot services its player and telemetry.
const tickPeriod = 5 * time.Millisecond

// Robot is the fake robot on a loopback UDP socket.
type Robot struct {
	conn *net.UDPConn
	mu   sync.Mutex
	core *Core
	done chan struct{}
}

// Listen opens the robot's socket on addr ("127.0.0.1:0" for any port).
func Listen(addr string) (*Robot, error) {
	ua, err := net.ResolveUDPAddr("udp4", addr)
	if err != nil {
		return nil, err
	}
	conn, err := net.ListenUDP("udp4", ua)
	if err != nil {
		return nil, err
	}
	return &Robot{conn: conn, core: NewCore(), done: make(chan struct{})}, nil
}

// Addr is the robot's address.
func (r *Robot) Addr() string { return r.conn.LocalAddr().String() }

// Run serves until ctx ends or the robot is closed.
func (r *Robot) Run(ctx context.Context) error {
	defer close(r.done)
	go func() {
		<-ctx.Done()
		_ = r.conn.Close()
	}()
	buf := make([]byte, 64)
	for {
		_ = r.conn.SetReadDeadline(time.Now().Add(tickPeriod))
		n, from, err := r.conn.ReadFromUDPAddrPort(buf)
		now := time.Now()
		switch {
		case err == nil:
			r.mu.Lock()
			r.core.Handle(append([]byte(nil), buf[:n]...), netip.AddrPortFrom(from.Addr().Unmap(), from.Port()), now)
			r.mu.Unlock()
		case errors.Is(err, net.ErrClosed):
			return nil
		case isTimeout(err):
		default:
			continue
		}
		r.mu.Lock()
		out := r.core.Tick(now)
		r.mu.Unlock()
		for _, d := range out {
			_, _ = r.conn.WriteToUDPAddrPort(d.B, d.To)
		}
	}
}

func isTimeout(err error) bool {
	var ne net.Error
	return errors.As(err, &ne) && ne.Timeout()
}

// Close closes the socket and waits for Run to end, if it runs.
func (r *Robot) Close() error {
	err := r.conn.Close()
	select {
	case <-r.done:
	case <-time.After(time.Second):
	}
	return err
}

// Snapshot is the robot's state now.
func (r *Robot) Snapshot() Snapshot {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.core.Snapshot()
}

// SetDurations replaces the motions' playing times (for faster tests).
func (r *Robot) SetDurations(d map[uint8]time.Duration) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.core.Durations = d
}

// SetHooks sets the test hooks: drop or corrupt every nth telemetry frame
// (0 for none).
func (r *Robot) SetHooks(dropEvery, corruptEvery int) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.core.DropEvery, r.core.CorruptEvery = dropEvery, corruptEvery
}
