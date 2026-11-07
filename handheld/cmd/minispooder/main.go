// Command minispooder is MiniSpooder.pak: it plays the robot's stock motions
// from a TrimUI Smart Pro over the robot's drive link (openspec change
// handheld-pak). It is the only package that imports gabagool or go-sdl2: it
// starts gabagool, reads the controls, draws the drive screen, shows the
// join progress and message and hands the Power button to the app. The rest is
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
	"github.com/BrandonKowalski/gabagool/v2/pkg/gabagool/constants"
	"github.com/veandco/go-sdl2/sdl"
	uatomic "go.uber.org/atomic"

	"github.com/blackfyre/mini-spooder/handheld/internal/app"
	"github.com/blackfyre/mini-spooder/handheld/internal/fonts"
)

// SDL wants its video and event calls on the thread that initialised it.
func init() { runtime.LockOSThread() }

const (
	// pollPeriod is how often the controls are read: well inside the
	// sender's 50 ms period.
	pollPeriod = 5 * time.Millisecond
	// drawPeriod is the drive screen's redraw (20 Hz).
	drawPeriod = 50 * time.Millisecond
	// signalGrace is how long a SIGINT or SIGTERM may take to end the run
	// through its context before the process restores WiFi power saving
	// and exits by itself (a gabagool message screen does not watch the
	// context).
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

	s, ok := join(ctx, cfg)
	if !ok {
		return 1
	}
	session.set(s)
	defer session.close()
	current.Store(s.App)

	runErr := make(chan error, 1)
	go func() { runErr <- s.Run(ctx) }()
	drive(s.App, runErr, !cfg.Device)
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

// join starts the run, showing the join message (or any other start-up
// error) with retry and quit until the start succeeds or the user quits:
// A or Start retries, Menu quits. On the device the start, which joins the
// robot's network, runs under a progress screen, where Menu quits too.
func join(ctx context.Context, cfg app.Config) (*app.Session, bool) {
	for {
		s, err := start(ctx, cfg)
		if err == nil {
			return s, true
		}
		if ctx.Err() != nil || errors.Is(err, errQuit) {
			return nil, false
		}
		_, err = gabagool.ConfirmationMessage(app.JoinMessage(err), []gabagool.FooterHelpItem{
			{ButtonName: "MENU", HelpText: "Quit"},
			{ButtonName: "A", HelpText: "Retry", IsConfirmButton: true},
		}, gabagool.MessageOptions{CancelButton: constants.VirtualButtonMenu})
		if err != nil || ctx.Err() != nil {
			return nil, false
		}
	}
}

// errQuit is Menu on the join's progress screen.
var errQuit = errors.New("quit while joining")

// start is app.Start, under a progress screen on the device. Menu on the
// screen ends the join, which gives the networks back, and quits
// (errQuit). However the screen ends, start ends the join and waits for it
// before returning, so no join outlives it; a session the screen did not
// hand over is closed.
func start(ctx context.Context, cfg app.Config) (*app.Session, error) {
	if !cfg.Device {
		return app.Start(ctx, cfg, app.OS())
	}
	jctx, cancel := context.WithCancel(ctx)
	defer cancel()
	// gabagool's progress bar reads go.uber.org/atomic's Float64.
	var progress uatomic.Float64
	sys := app.OS()
	sys.Progress = progress.Store
	type result struct {
		s   *app.Session
		err error
	}
	done := make(chan result, 1)
	_, screenErr := gabagool.ProcessMessage(app.JoiningMessage, gabagool.ProcessMessageOptions{
		ShowProgressBar: true,
		Progress:        &progress,
		CancelButton:    constants.VirtualButtonMenu,
		FooterHelpItems: []gabagool.FooterHelpItem{{ButtonName: "MENU", HelpText: "Quit"}},
	}, func() (struct{}, error) {
		s, err := app.Start(jctx, cfg, sys)
		done <- result{s, err}
		return struct{}{}, err
	})
	var r result
	select {
	case r = <-done:
	default:
		// The screen ended first (Menu, an SDL quit).
		cancel()
		r = <-done
	}
	if errors.Is(screenErr, gabagool.ErrCancelled) {
		screenErr = errQuit
	}
	if screenErr != nil && !errors.Is(screenErr, r.err) {
		if r.s != nil {
			_ = r.s.Close()
		}
		return nil, screenErr
	}
	return r.s, r.err
}

// drive is the main thread's loop while the app drives: it polls SDL for
// the controls and the Menu button and redraws the drive screen, until the
// run ends.
func drive(a *app.App, runErr <-chan error, keyboard bool) {
	p := newPad(keyboard)
	defer p.close()
	win := gabagool.GetWindow()
	scr, err := newScreen(win, win.Renderer)
	if err != nil {
		fmt.Fprintln(os.Stderr, "minispooder:", err)
		a.Press(app.ActionExit, time.Now())
	}
	defer func() {
		if scr != nil {
			scr.close()
		}
	}()

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
		if now := time.Now(); scr != nil && now.Sub(lastDraw) >= drawPeriod {
			lastDraw = now
			scr.draw(a.Screen(now))
		}
		sdl.Delay(uint32(pollPeriod / time.Millisecond))
	}
}
