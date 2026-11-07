package app

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/udplink"
	"github.com/blackfyre/mini-spooder/handheld/internal/wlan"
)

// Config is the command line.
type Config struct {
	// Device: running on the TrimUI under NextUI, so the WiFi steps and the
	// join run and the robot's fixed address is used.
	Device bool
	// Addr is the robot's address and port without Device (desktop Linux,
	// the fake robot).
	Addr string
}

const usageText = `Usage: minispooder [flags]

Plays the robot's stock motions over its drive link. On the TrimUI the pak's
launch.sh runs it with --device; on desktop Linux give --addr instead, such as
the fake robot's --listen address.

Flags:
`

// ParseFlags reads the command line. It returns flag.ErrHelp after printing
// the help for -h or --help.
func ParseFlags(args []string, out io.Writer) (Config, error) {
	var c Config
	fs := flag.NewFlagSet("minispooder", flag.ContinueOnError)
	fs.SetOutput(out)
	fs.BoolVar(&c.Device, "device", false, "run on the handheld: check wlan0, manage WiFi power saving, drive "+udplink.DefaultAddr)
	fs.StringVar(&c.Addr, "addr", udplink.DefaultAddr, "the robot's `address:port` (not with --device)")
	fs.Usage = func() {
		fmt.Fprint(out, usageText)
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return c, err
	}
	if fs.NArg() > 0 {
		fs.Usage()
		return c, fmt.Errorf("unexpected argument %q", fs.Arg(0))
	}
	addrSet := false
	fs.Visit(func(f *flag.Flag) { addrSet = addrSet || f.Name == "addr" })
	if c.Device && addrSet {
		return c, errors.New("--addr cannot be used with --device: on the handheld the robot is " + udplink.DefaultAddr)
	}
	if c.Device {
		c.Addr = udplink.DefaultAddr
	}
	return c, nil
}

// System is what Start needs of the system, injectable for the tests.
type System struct {
	Runner   wlan.Runner
	Addrs    wlan.Addrs
	ReadFile func(string) ([]byte, error)
	// Local is the socket's own address, "" for any (the tests reuse a
	// source port).
	Local string
	// Progress, if not nil, gets the share of the join done, from 0 to 1.
	Progress func(float64)
}

// OS is the running system.
func OS() System {
	return System{Runner: wlan.ExecRunner{}, Addrs: wlan.InterfaceAddrs, ReadFile: os.ReadFile}
}

// Session is one started run: the link and the app driving through it.
type Session struct {
	App  *App
	Link *udplink.Link
	// Notice is a start-up problem worth showing that did not stop the
	// start, or "".
	Notice string
	power  *wlan.PowerSave
	pin    *wlan.Pin
}

// Start prepares a run. With Device it first brings wlan0 onto the robot's
// network (wlan.Join; a *wlan.NotJoinedError when that fails, with the
// other networks given back: show its message and call Start again to
// retry), then reads WiFi power saving and turns it off; without Device it
// runs no WiFi command at all. Then it
// makes the link's socket, whose silence starts now. Nothing is sent until
// Run, and nothing in the silence.
func Start(ctx context.Context, cfg Config, sys System) (*Session, error) {
	s := &Session{}
	var power PowerSave
	var readFile func(string) ([]byte, error)
	var run wlan.Runner
	if cfg.Device {
		pin, err := wlan.Join(ctx, sys.Runner, sys.Addrs, sys.Progress)
		if err != nil {
			return nil, err
		}
		s.pin = pin
		// A setting that cannot be read is left alone: driving with power
		// saving on is slower to react, not unsafe.
		p, err := wlan.ReadPowerSave(ctx, sys.Runner)
		if err != nil {
			s.Notice = err.Error()
		} else {
			s.power, power = p, p
			if err := p.Off(ctx); err != nil {
				s.Notice = err.Error()
			}
		}
		readFile, run = sys.ReadFile, sys.Runner
	}
	link, err := udplink.Dial(cfg.Addr, sys.Local)
	if err != nil {
		s.Close()
		return nil, err
	}
	s.Link = link
	s.App = New(link, run, power, readFile)
	s.App.SampleSignal(ctx)
	if s.Notice != "" {
		s.App.setNotice("%s", s.Notice)
	}
	return s, nil
}

// Run sends until ctx ends or a leave ends the run, sampling the signal
// level every SignalPeriod meanwhile.
func (s *Session) Run(ctx context.Context) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	sampled := make(chan struct{})
	go func() {
		defer close(sampled)
		t := time.NewTicker(SignalPeriod)
		defer t.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case <-t.C:
				s.App.SampleSignal(ctx)
			}
		}
	}()
	err := s.Link.Run(ctx, s.App.Step)
	cancel()
	<-sampled
	return err
}

// Close closes the link, restores the WiFi power-saving setting read at the
// start and, when Start joined the robot's network, gives the other saved
// networks back. It is safe to call more than once.
func (s *Session) Close() error {
	var errs []error
	if s.Link != nil {
		errs = append(errs, s.Link.Close())
		s.Link = nil
	}
	if s.power != nil {
		errs = append(errs, s.power.Restore(context.Background()))
	}
	if s.pin != nil {
		errs = append(errs, s.pin.Restore())
	}
	return errors.Join(errs...)
}
