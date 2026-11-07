package wlan

import (
	"context"
	"errors"
	"net"
	"os"
	"strings"
	"testing"
	"time"
)

// fakeRunner records commands and answers them from a table keyed by the
// command line; after, when set, runs after each command.
type fakeRunner struct {
	calls   []string
	answers map[string]string
	fail    map[string]bool
	after   func(cmd string)
}

func (f *fakeRunner) Run(_ context.Context, name string, args ...string) ([]byte, error) {
	cmd := strings.Join(append([]string{name}, args...), " ")
	f.calls = append(f.calls, cmd)
	if f.after != nil {
		f.after(cmd)
	}
	if f.fail[cmd] {
		return nil, errors.New("exit status 1")
	}
	return []byte(f.answers[cmd]), nil
}

func (f *fakeRunner) count(cmd string) int {
	n := 0
	for _, c := range f.calls {
		if c == cmd {
			n++
		}
	}
	return n
}

// fakeAddrs is wlan0 holding ip, which renew may change.
type fakeAddrs struct{ ip string }

func (a *fakeAddrs) addrs(iface string) ([]net.IP, error) {
	if iface != Interface {
		return nil, errors.New("no such interface")
	}
	if a.ip == "" {
		return nil, nil
	}
	return []net.IP{net.ParseIP("fe80::1"), net.ParseIP(a.ip)}, nil
}

const renew = "udhcpc -i wlan0 -n -q -t 5"

func TestJoined(t *testing.T) {
	run := &fakeRunner{}
	a := &fakeAddrs{ip: "192.168.4.2"}
	if err := CheckAddress(context.Background(), run, a.addrs); err != nil {
		t.Fatal(err)
	}
	if len(run.calls) != 0 {
		t.Fatalf("ran %v, want nothing", run.calls)
	}
}

func TestStaleLeaseRenewed(t *testing.T) {
	a := &fakeAddrs{ip: "192.168.0.23"}
	run := &fakeRunner{after: func(cmd string) {
		if cmd == renew {
			a.ip = "192.168.4.2"
		}
	}}
	if err := CheckAddress(context.Background(), run, a.addrs); err != nil {
		t.Fatal(err)
	}
	if len(run.calls) != 1 || run.count(renew) != 1 {
		t.Fatalf("ran %v, want one %q", run.calls, renew)
	}
}

func TestNotJoinedNamesTheNetwork(t *testing.T) {
	a := &fakeAddrs{ip: "192.168.0.23"}
	run := &fakeRunner{answers: map[string]string{
		"iw dev wlan0 link": "Connected to 11:22:33:44:55:66 (on wlan0)\n\tSSID: Home WiFi\n\tfreq: 2412\n",
	}}
	err := CheckAddress(context.Background(), run, a.addrs)
	var nj *NotJoinedError
	if !errors.As(err, &nj) {
		t.Fatalf("got %v, want a NotJoinedError", err)
	}
	if run.count(renew) != 1 {
		t.Fatalf("ran %v, want one renewal", run.calls)
	}
	msg := err.Error()
	for _, want := range []string{`"mini-spooder"`, "87654321", "NextUI's WiFi settings", `"Home WiFi"`} {
		if !strings.Contains(msg, want) {
			t.Fatalf("join message %q lacks %q", msg, want)
		}
	}
}

func TestNotJoinedOnNoNetwork(t *testing.T) {
	a := &fakeAddrs{}
	run := &fakeRunner{answers: map[string]string{"iw dev wlan0 link": "Not connected.\n"}}
	err := CheckAddress(context.Background(), run, a.addrs)
	if err == nil || !strings.Contains(err.Error(), "on no network") {
		t.Fatalf("got %v, want the join message naming no network", err)
	}
	if run.count(renew) != 1 {
		t.Fatalf("ran %v, want one renewal", run.calls)
	}
}

func TestPowerSaveOnIsRestored(t *testing.T) {
	run := &fakeRunner{answers: map[string]string{"iw dev wlan0 get power_save": "Power save: on\n"}}
	ctx := context.Background()
	p, err := ReadPowerSave(ctx, run)
	if err != nil {
		t.Fatal(err)
	}
	if err := p.Off(ctx); err != nil {
		t.Fatal(err)
	}
	if err := p.Restore(ctx); err != nil {
		t.Fatal(err)
	}
	want := []string{"iw dev wlan0 get power_save", "iw dev wlan0 set power_save off", "iw dev wlan0 set power_save on"}
	if strings.Join(run.calls, "|") != strings.Join(want, "|") {
		t.Fatalf("ran %v, want %v", run.calls, want)
	}
}

func TestPowerSaveOffStaysOff(t *testing.T) {
	run := &fakeRunner{answers: map[string]string{"iw dev wlan0 get power_save": "Power save: off\n"}}
	ctx := context.Background()
	p, err := ReadPowerSave(ctx, run)
	if err != nil {
		t.Fatal(err)
	}
	_ = p.Off(ctx)
	_ = p.Restore(ctx)
	if run.count("iw dev wlan0 set power_save on") != 0 {
		t.Fatalf("ran %v: power save turned on", run.calls)
	}
}

func TestPowerSaveUnreadable(t *testing.T) {
	for name, run := range map[string]*fakeRunner{
		"fails":   {fail: map[string]bool{"iw dev wlan0 get power_save": true}},
		"garbled": {answers: map[string]string{"iw dev wlan0 get power_save": "command failed: No such device (-19)\n"}},
	} {
		if _, err := ReadPowerSave(context.Background(), run); err == nil {
			t.Fatalf("%s: read succeeded", name)
		}
	}
}

const wireless = `Inter-| sta-|   Quality        |   Discarded packets               | Missed | WE
 face | tus | link level noise |  nwid  crypt   frag  retry   misc | beacon | 22
 wlan0: 0000   70.  -40.  -256        0      0      0      0      0        0
`

const iwLink = `Connected to 02:00:00:00:00:00 (on wlan0)
	SSID: mini-spooder
	freq: 2412
	signal: -47 dBm
	rx bitrate: 72.2 MBit/s
`

func TestSignal(t *testing.T) {
	read := func(content string, err error) func(string) ([]byte, error) {
		return func(path string) ([]byte, error) {
			if path != WirelessPath {
				t.Fatalf("read %s", path)
			}
			return []byte(content), err
		}
	}
	link := "iw dev wlan0 link"
	ctx := context.Background()

	// The kernel's statistics first, without running iw.
	run := &fakeRunner{answers: map[string]string{link: iwLink}}
	if dbm, err := Signal(ctx, run, read(wireless, nil)); err != nil || dbm != -40 {
		t.Fatalf("signal %d, %v; want -40 dBm", dbm, err)
	}
	if run.count(link) != 0 {
		t.Fatal("ran iw with the statistics present")
	}
	// No file (the TrimUI) or no wlan0 line: iw's link.
	noLine := strings.Join(strings.Split(wireless, "\n")[:2], "\n")
	for _, c := range []struct {
		content string
		err     error
	}{{"", os.ErrNotExist}, {noLine, nil}} {
		if dbm, err := Signal(ctx, run, read(c.content, c.err)); err != nil || dbm != -47 {
			t.Fatalf("from iw: signal %d, %v; want -47 dBm", dbm, err)
		}
	}
	// Neither: not connected, or iw failing.
	notConnected := &fakeRunner{answers: map[string]string{link: "Not connected.\n"}}
	if _, err := Signal(ctx, notConnected, read("", os.ErrNotExist)); !errors.Is(err, ErrNoSignal) {
		t.Fatalf("not connected: %v, want unknown", err)
	}
	failing := &fakeRunner{fail: map[string]bool{link: true}}
	if _, err := Signal(ctx, failing, read("", os.ErrNotExist)); !errors.Is(err, ErrNoSignal) {
		t.Fatalf("iw failing: %v, want unknown", err)
	}
}

// Retry after the join message: the check renews once more, and passes once
// the user has joined.
func TestRetryRenewsAgain(t *testing.T) {
	a := &fakeAddrs{ip: "192.168.0.23"}
	run := &fakeRunner{}
	if err := CheckAddress(context.Background(), run, a.addrs); err == nil {
		t.Fatal("check passed off the robot's network")
	}
	a.ip = "192.168.4.3"
	if err := CheckAddress(context.Background(), run, a.addrs); err != nil {
		t.Fatal(err)
	}
	if run.count(renew) != 1 {
		t.Fatalf("ran %v, want one renewal in all (none on the passing retry)", run.calls)
	}
}

// A slow iw is bounded by the caller's context (the app's 500 ms).
func TestSignalBoundedByContext(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Millisecond)
	defer cancel()
	slow := runnerFunc(func(ctx context.Context, name string, args ...string) ([]byte, error) {
		<-ctx.Done()
		return nil, ctx.Err()
	})
	start := time.Now()
	_, err := Signal(ctx, slow, func(string) ([]byte, error) { return nil, os.ErrNotExist })
	if !errors.Is(err, ErrNoSignal) || time.Since(start) > time.Second {
		t.Fatalf("slow iw: %v after %v, want unknown at the bound", err, time.Since(start))
	}
}

type runnerFunc func(ctx context.Context, name string, args ...string) ([]byte, error)

func (f runnerFunc) Run(ctx context.Context, name string, args ...string) ([]byte, error) {
	return f(ctx, name, args...)
}
