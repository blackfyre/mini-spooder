// Command minispooder is MiniSpooder.pak: it plays the robot's stock motions
// from a TrimUI Smart Pro over the robot's drive link (openspec change
// handheld-pak). It is the only package that imports gabagool or go-sdl2: it
// starts gabagool, reads the controls, draws the join and drive screens in
// the Blackfyre style and hands the Power button to the app. The rest is
// internal/app, which the tests and the e2e drive without SDL.
//
// On the handheld the pak's launch.sh runs it with --device; on desktop
// Linux it takes --addr, such as the fake robot's --listen address.
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"math"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"slices"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/BrandonKowalski/gabagool/v2/pkg/gabagool"
	"github.com/veandco/go-sdl2/sdl"

	"github.com/blackfyre/mini-spooder/handheld/internal/app"
	"github.com/blackfyre/mini-spooder/handheld/internal/fonts"
	"github.com/blackfyre/mini-spooder/handheld/internal/padmap"
)

// SDL wants its video and event calls on the thread that initialised it.
func init() { runtime.LockOSThread() }

const (
	// pollPeriod is how often the controls are read: well inside the
	// sender's 50 ms period.
	pollPeriod = 5 * time.Millisecond
	// drawPeriod is the join and drive screens' redraw (20 Hz).
	drawPeriod = 50 * time.Millisecond
	// signalGrace is how long a SIGINT or SIGTERM may take to end the run
	// through its context before the process restores WiFi power saving
	// and exits by itself (the run's start does not return until the join
	// has ended).
	signalGrace = time.Second
)

func main() { os.Exit(run()) }

func run() int {
	if slices.Contains(os.Args[1:], "--licences") {
		fmt.Println("Archivo:\n\n" + fonts.ArchivoLicence + "\n\nJetBrains Mono:\n\n" + fonts.JetBrainsMonoLicence)
		return 0
	}
	cfg, err := app.ParseFlags(os.Args[1:], os.Stderr)
	if errors.Is(err, flag.ErrHelp) {
		return 0
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "minispooder:", err)
		return 2
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM, syscall.SIGHUP)
	defer stop()

	var current atomic.Pointer[app.App]
	var session sessionHolder
	go func() {
		<-ctx.Done()
		time.Sleep(signalGrace)
		session.close()
		os.Exit(1)
	}()

	opts := gabagool.Options{WindowTitle: "MiniSpooder", IsNextUI: cfg.Device, LogPath: logPath()}
	if cfg.Device {
		// The patched gabagool (design D5); off the device the Power button
		// is left unhandled.
		opts.PowerButtonHook = func(long bool) { powerButton(&current, long) }
	}
	gabagool.Init(opts)
	defer gabagool.Close()

	win := gabagool.GetWindow()
	scr, err := newScreen(win, win.Renderer)
	if err != nil {
		fmt.Fprintln(os.Stderr, "minispooder:", err)
		return 1
	}
	defer scr.close()
	p := newPad(!cfg.Device)
	defer p.close()

	s, ok := join(ctx, cfg, scr, p)
	if !ok {
		return 1
	}
	session.set(s)
	defer session.close()
	current.Store(s.App)

	runErr := make(chan error, 1)
	go func() { runErr <- s.Run(ctx) }()
	drive(s.App, runErr, scr, p)
	if ctx.Err() != nil {
		return 1
	}
	return 0
}

// logPath is gabagool's log: in NextUI's log directory on the device, else
// in the temporary directory.
func logPath() string {
	if dir := os.Getenv("LOGS_PATH"); dir != "" {
		return filepath.Join(dir, "MiniSpooder.gabagool.txt")
	}
	return filepath.Join(os.TempDir(), "minispooder-gabagool.log")
}

// sessionHolder lets the signal backstop close the session the main
// goroutine opened.
type sessionHolder struct {
	mu   sync.Mutex
	s    *app.Session
	once sync.Once
}

func (h *sessionHolder) set(s *app.Session) {
	h.mu.Lock()
	h.s = s
	h.mu.Unlock()
}

// close closes the session once: the link, and the WiFi power-saving
// setting restored.
func (h *sessionHolder) close() {
	h.once.Do(func() {
		h.mu.Lock()
		s := h.s
		h.mu.Unlock()
		if s != nil {
			if err := s.Close(); err != nil {
				fmt.Fprintln(os.Stderr, "minispooder:", err)
			}
		}
	})
}

// powerButton is gabagool's power-button hook. While the app drives, the
// press starts a leave, which runs the suspend script or the power-off
// command once Standby has finished or the wait has ended. Before the link
// has started (the join message) there is nothing to bring to Standby, so
// the press acts at once with no frame sent.
func powerButton(current *atomic.Pointer[app.App], long bool) {
	act, command := app.ActionSuspend, app.SuspendScript
	if long {
		act, command = app.ActionPowerOff, app.PowerOffCommand
	}
	if a := current.Load(); a != nil {
		a.Press(act, time.Now())
		return
	}
	if err := exec.Command(command).Run(); err != nil {
		fmt.Fprintf(os.Stderr, "minispooder: %s: %v\n", command, err)
	}
}

// join starts the run, showing the join screen with any start-up error
// (the join instructions) and retry and quit until the start succeeds or
// the user quits: A or Start retries, Menu quits. On the device the start,
// which joins the robot's network, runs under the join screen with its
// progress, where Menu quits too.
func join(ctx context.Context, cfg app.Config, scr *screen, p *pad) (*app.Session, bool) {
	for {
		s, err := start(ctx, cfg, scr, p)
		if err == nil {
			return s, true
		}
		if ctx.Err() != nil || errors.Is(err, errQuit) {
			return nil, false
		}
		if !retry(ctx, app.JoinFailedView(err), scr, p) {
			return nil, false
		}
	}
}

// retry shows v until A or Start is pressed (true) or Menu, a window close
// or ctx's end (false). A button already held when it starts must be
// released first.
func retry(ctx context.Context, v app.JoinView, scr *screen, p *pad) bool {
	const again = padmap.A | padmap.Start
	held := p.controls().Buttons & again
	for ctx.Err() == nil {
		for ev := sdl.PollEvent(); ev != nil; ev = sdl.PollEvent() {
			if p.event(ev) {
				return false
			}
		}
		b := p.controls().Buttons & again
		if b&^held != 0 {
			return true
		}
		held &= b
		scr.drawJoin(v)
		sdl.Delay(uint32(drawPeriod / time.Millisecond))
	}
	return false
}

// errQuit is Menu on the join screen while the join runs.
var errQuit = errors.New("quit while joining")

// start is app.Start, under the join screen on the device. Menu (or a
// window close) ends the join, which gives the networks back, and quits
// (errQuit). start waits for the join to end before returning, so no join
// outlives it; a session that arrives after a quit is closed.
func start(ctx context.Context, cfg app.Config, scr *screen, p *pad) (*app.Session, error) {
	if !cfg.Device {
		return app.Start(ctx, cfg, app.OS())
	}
	jctx, cancel := context.WithCancel(ctx)
	defer cancel()
	var progress atomic.Uint64 // float64 bits
	sys := app.OS()
	sys.Progress = func(v float64) { progress.Store(math.Float64bits(v)) }
	type result struct {
		s   *app.Session
		err error
	}
	done := make(chan result, 1)
	go func() {
		s, err := app.Start(jctx, cfg, sys)
		done <- result{s, err}
	}()
	quit := false
	for {
		select {
		case r := <-done:
			if quit {
				if r.s != nil {
					_ = r.s.Close()
				}
				return nil, errQuit
			}
			return r.s, r.err
		default:
		}
		for ev := sdl.PollEvent(); ev != nil; ev = sdl.PollEvent() {
			if p.event(ev) && !quit {
				quit = true
				cancel()
			}
		}
		scr.drawJoin(app.JoiningView(math.Float64frombits(progress.Load())))
		sdl.Delay(uint32(drawPeriod / time.Millisecond))
	}
}

// drive is the main thread's loop while the app drives: it polls SDL for
// the controls and the Menu button and redraws the drive screen, until the
// run ends.
func drive(a *app.App, runErr <-chan error, scr *screen, p *pad) {
	var lastDraw time.Time
	for {
		select {
		case err := <-runErr:
			if err != nil && !errors.Is(err, context.Canceled) {
				fmt.Fprintln(os.Stderr, "minispooder:", err)
			}
			return
		default:
		}
		for ev := sdl.PollEvent(); ev != nil; ev = sdl.PollEvent() {
			if p.event(ev) {
				a.Press(app.ActionExit, time.Now())
			}
		}
		a.SetControls(p.controls(), time.Now())
		if now := time.Now(); now.Sub(lastDraw) >= drawPeriod {
			lastDraw = now
			scr.draw(a.Screen(now))
		}
		sdl.Delay(uint32(pollPeriod / time.Millisecond))
	}
}
