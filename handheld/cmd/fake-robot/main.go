// Command fake-robot is the stand-in robot for running the desktop build
// without hardware (openspec change handheld-pak, design D10): it speaks the
// drive link on a loopback UDP address and logs each request it makes.
//
//	fake-robot --listen 127.0.0.1:52041
package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/fakerobot"
	"github.com/blackfyre/mini-spooder/handheld/internal/motions"
)

func main() {
	listen := flag.String("listen", "127.0.0.1:52041", "the `address:port` to listen on")
	corrupt := flag.Int("corrupt-every", 0, "corrupt every `n`th telemetry frame (0: none)")
	drop := flag.Int("drop-every", 0, "drop every `n`th telemetry frame (0: none)")
	flag.Parse()

	r, err := fakerobot.Listen(*listen)
	if err != nil {
		fmt.Fprintln(os.Stderr, "fake-robot:", err)
		os.Exit(1)
	}
	r.SetHooks(*drop, *corrupt)
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()
	fmt.Printf("fake-robot: listening on %s\n", r.Addr())

	go func() {
		seen, sessions := 0, 0
		t := time.NewTicker(50 * time.Millisecond)
		defer t.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case <-t.C:
			}
			s := r.Snapshot()
			if s.Sessions != sessions {
				sessions = s.Sessions
				fmt.Printf("fake-robot: session %d from %v\n", sessions, s.Locked)
			}
			for _, q := range s.Requests[seen:] {
				what := "stop"
				if q.Motion != 0 {
					what = motions.Name(q.Motion)
				}
				fmt.Printf("fake-robot: request %s (counter %d): %v\n", what, q.Counter, q.Result)
			}
			seen = len(s.Requests)
		}
	}()
	if err := r.Run(ctx); err != nil {
		fmt.Fprintln(os.Stderr, "fake-robot:", err)
		os.Exit(1)
	}
}
