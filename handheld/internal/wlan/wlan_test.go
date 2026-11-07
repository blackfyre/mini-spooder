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

const (
	renew    = "udhcpc -i wlan0 -n -q -t 5"
	wpa      = "wpa_cli -p /etc/wifi/sockets -i wlan0 "
	list     = wpa + "list_networks"
	selectID = wpa + "select_network 6"
	status   = wpa + "status"
	enable   = wpa + "enable_network all"
	reassoc  = wpa + "reassociate"
)

// listing is the TrimUI's saved networks as wpa_cli lists them.
const listing = "network id / ssid / bssid / flags\n" +
	"0\tmeki-net\tany\t[CURRENT]\n" +
	"1\tSpider Robot\tany\t\n" +
	"5\tspooder-drive-6da9\tany\t\n" +
	"6\tmini-spooder\tany\t\n" +
	"7\tmini-spooder-old\tany\t\n"

const completed = "bssid=02:00:00:00:00:01\nfreq=2412\nssid=mini-spooder\nid=6\nmode=station\npairwise_cipher=CCMP\ngroup_cipher=TKIP\nkey_mgmt=WPA2-PSK\nwpa_state=COMPLETED\n"

// shortBounds shortens the join's bounds for a test.
func shortBounds(t *testing.T) {
	t.Helper()
	a, l, p := AssociateWithin, LeaseWithin, pollEvery
	AssociateWithin, LeaseWithin, pollEvery = 60*time.Millisecond, time.Second, 5*time.Millisecond
	t.Cleanup(func() { AssociateWithin, LeaseWithin, pollEvery = a, l, p })
}

// joinFake is the TrimUI on its home network with the robot's network
// saved as id 6: it associates at once, and the lease gives lease (none
// when "").
func joinFake(lease string) (*fakeRunner, *fakeAddrs) {
	a := &fakeAddrs{ip: "192.168.0.84"}
	run := &fakeRunner{
		answers: map[string]string{list: listing, selectID: "OK\n", status: completed, enable: "OK\n", reassoc: "OK\n"},
		fail:    map[string]bool{},
		after: func(cmd string) {
			if cmd == renew && lease != "" {
				a.ip = lease
			}
		},
	}
	return run, a
}

// noSave fails the test if the supplicant's configuration was saved.
func noSave(t *testing.T, run *fakeRunner) {
	t.Helper()
	for _, c := range run.calls {
		if strings.Contains(c, "save_config") {
			t.Errorf("saved the supplicant's configuration: %v", run.calls)
		}
	}
}

func TestJoinedRunsNothing(t *testing.T) {
	run := &fakeRunner{}
	a := &fakeAddrs{ip: "192.168.4.2"}
	pin, err := Join(context.Background(), run, a.addrs, nil)
	if err != nil || pin != nil {
		t.Fatalf("got %v, %v; want no pin and no error", pin, err)
	}
	if len(run.calls) != 0 {
		t.Fatalf("ran %v, want nothing", run.calls)
	}
}

func TestJoinSelectsWaitsAndLeases(t *testing.T) {
	shortBounds(t)
	run, a := joinFake("192.168.4.101")
	var shares []float64
	pin, err := Join(context.Background(), run, a.addrs, func(f float64) { shares = append(shares, f) })
	if err != nil || pin == nil {
		t.Fatalf("got %v, %v; want a pin", pin, err)
	}
	want := []string{list, selectID, status, renew}
	if strings.Join(run.calls, "|") != strings.Join(want, "|") {
		t.Fatalf("ran %v, want %v", run.calls, want)
	}
	if len(shares) == 0 || shares[len(shares)-1] != 1 {
		t.Errorf("progress %v, want it to end at 1", shares)
	}
	// The other networks stay disabled until the Restore, which acts once.
	for i := 0; i < 2; i++ {
		if err := pin.Restore(); err != nil {
			t.Fatal(err)
		}
	}
	if run.count(enable) != 1 || run.count(reassoc) != 1 || run.calls[len(run.calls)-2] != enable {
		t.Fatalf("ran %v, want one enable_network all then one reassociate", run.calls)
	}
	noSave(t, run)
}

// The association is polled until the supplicant reports COMPLETED on the
// robot's network.
func TestJoinWaitsForAssociation(t *testing.T) {
	shortBounds(t)
	AssociateWithin = 5 * time.Second
	run, a := joinFake("192.168.4.101")
	polls := 0
	run.after = func(cmd string) {
		if cmd == status {
			polls++
			if polls == 3 {
				run.answers[status] = completed
			} else {
				run.answers[status] = "wpa_state=ASSOCIATING\n"
			}
		}
		if cmd == renew {
			a.ip = "192.168.4.101"
		}
	}
	if _, err := Join(context.Background(), run, a.addrs, nil); err != nil {
		t.Fatal(err)
	}
	if polls != 3 || run.count(renew) != 1 {
		t.Fatalf("ran %v, want three status polls and one lease", run.calls)
	}
}

func TestJoinNoSavedNetwork(t *testing.T) {
	run, a := joinFake("")
	run.answers[list] = "network id / ssid / bssid / flags\n0\tmeki-net\tany\t[CURRENT]\n"
	run.answers["iw dev wlan0 link"] = "Connected to 11:22:33:44:55:66 (on wlan0)\n\tSSID: meki-net\n\tfreq: 2412\n"
	pin, err := Join(context.Background(), run, a.addrs, nil)
	var nj *NotJoinedError
	if pin != nil || !errors.As(err, &nj) || nj.Problem != "" {
		t.Fatalf("got %v, %v; want the join message", pin, err)
	}
	for _, want := range []string{`"mini-spooder"`, "87654321", "NextUI's WiFi settings", `"meki-net"`} {
		if !strings.Contains(err.Error(), want) {
			t.Fatalf("join message %q lacks %q", err, want)
		}
	}
	// Nothing was changed, so nothing is given back.
	for _, c := range run.calls {
		if strings.HasPrefix(c, wpa) && c != list {
			t.Fatalf("ran %v, want only the listing", run.calls)
		}
	}
}

func TestJoinNoSavedNetworkOnNoNetwork(t *testing.T) {
	run, a := joinFake("")
	a.ip = ""
	run.answers[list] = "network id / ssid / bssid / flags\n"
	run.answers["iw dev wlan0 link"] = "Not connected.\n"
	_, err := Join(context.Background(), run, a.addrs, nil)
	if err == nil || !strings.Contains(err.Error(), "on no network") {
		t.Fatalf("got %v, want the join message naming no network", err)
	}
}

// Every failure after the selection gives the networks back and names the
// step that failed.
func TestJoinFailuresRestore(t *testing.T) {
	for _, c := range []struct {
		name, problem string
		lease         string
		setup         func(run *fakeRunner)
		lease0        bool // the lease was requested
	}{
		{name: "select refused", problem: "selecting it failed (FAIL)", setup: func(r *fakeRunner) { r.answers[selectID] = "FAIL\n" }},
		{name: "select fails", problem: "selecting it failed", setup: func(r *fakeRunner) { r.fail[selectID] = true }},
		{name: "join timeout", problem: "did not connect within", setup: func(r *fakeRunner) { r.answers[status] = "wpa_state=SCANNING\n" }},
		{name: "other network", problem: "did not connect within", setup: func(r *fakeRunner) {
			r.answers[status] = strings.Replace(completed, "ssid=mini-spooder", "ssid=meki-net", 1)
		}},
		{name: "DHCP failure", problem: "DHCP server gave no address", lease0: true, setup: func(r *fakeRunner) { r.fail[renew] = true }},
		{name: "wrong subnet", problem: "not in 192.168.4.0/24 (wlan0 holds 192.168.0.84)", lease0: true, setup: func(*fakeRunner) {}},
	} {
		t.Run(c.name, func(t *testing.T) {
			shortBounds(t)
			run, a := joinFake(c.lease)
			c.setup(run)
			pin, err := Join(context.Background(), run, a.addrs, nil)
			var nj *NotJoinedError
			if pin != nil || !errors.As(err, &nj) || !strings.Contains(nj.Problem, c.problem) {
				t.Fatalf("got %v, %v; want a join error with %q", pin, err, c.problem)
			}
			if !strings.Contains(err.Error(), `"mini-spooder"`) {
				t.Errorf("message %q does not name the robot's network", err)
			}
			if run.count(enable) != 1 || run.count(reassoc) != 1 || run.calls[len(run.calls)-1] != reassoc {
				t.Fatalf("ran %v, want the networks given back last", run.calls)
			}
			if (run.count(renew) == 1) != c.lease0 {
				t.Fatalf("ran %v, lease requested: want %v", run.calls, c.lease0)
			}
			noSave(t, run)
		})
	}
}

// A signal (ctx's end) during the wait gives the networks back too.
func TestJoinCancelledRestores(t *testing.T) {
	shortBounds(t)
	AssociateWithin = 5 * time.Second
	run, a := joinFake("")
	ctx, cancel := context.WithCancel(context.Background())
	run.answers[status] = "wpa_state=SCANNING\n"
	run.after = func(cmd string) {
		if cmd == status {
			cancel()
		}
	}
	_, err := Join(ctx, run, a.addrs, nil)
	if !errors.Is(err, context.Canceled) {
		t.Fatalf("got %v, want cancelled", err)
	}
	if run.count(enable) != 1 || run.count(reassoc) != 1 {
		t.Fatalf("ran %v, want the networks given back", run.calls)
	}
}

func TestJoinSupplicantDown(t *testing.T) {
	run, a := joinFake("")
	run.fail[list] = true
	_, err := Join(context.Background(), run, a.addrs, nil)
	var nj *NotJoinedError
	if !errors.As(err, &nj) || !strings.Contains(nj.Problem, "supplicant") {
		t.Fatalf("got %v, want a join error naming the supplicant", err)
	}
	if run.count(enable) != 0 {
		t.Fatalf("ran %v: nothing to give back", run.calls)
	}
}

// Retry after a failed join: the next join selects again, and its pin is
// its own.
func TestJoinRetry(t *testing.T) {
	shortBounds(t)
	run, a := joinFake("")
	if _, err := Join(context.Background(), run, a.addrs, nil); err == nil {
		t.Fatal("join passed with no lease")
	}
	run.after = func(cmd string) {
		if cmd == renew {
			a.ip = "192.168.4.3"
		}
	}
	pin, err := Join(context.Background(), run, a.addrs, nil)
	if err != nil || pin == nil {
		t.Fatalf("retry: %v, %v", pin, err)
	}
	if run.count(selectID) != 2 || run.count(enable) != 1 {
		t.Fatalf("ran %v, want two selections and one give-back so far", run.calls)
	}
	_ = pin.Restore()
	if run.count(enable) != 2 {
		t.Fatalf("ran %v, want the retry's pin to give back again", run.calls)
	}
}

func TestSavedNetwork(t *testing.T) {
	if id := SavedNetwork([]byte(listing), "mini-spooder"); id != "6" {
		t.Errorf("id %q, want 6", id)
	}
	if id := SavedNetwork([]byte(listing), "Spider Robot"); id != "1" {
		t.Errorf("id %q, want 1", id)
	}
	if id := SavedNetwork([]byte("network id / ssid / bssid / flags\r\n3\tmini-spooder\tany\t[DISABLED]\r\n"), "mini-spooder"); id != "3" {
		t.Errorf("CRLF listing: id %q, want 3", id)
	}
	for _, l := range []string{"", "network id / ssid / bssid / flags\n", "x\tmini-spooder\tany\t\n"} {
		if id := SavedNetwork([]byte(l), "mini-spooder"); id != "" {
			t.Errorf("%q: id %q, want none", l, id)
		}
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
