package app

import (
	"context"
	"errors"
	"io"
	"net"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/blackfyre/mini-spooder/handheld/internal/udplink"
	"github.com/blackfyre/mini-spooder/handheld/internal/wlan"
)

func TestParseFlags(t *testing.T) {
	c, err := ParseFlags(nil, io.Discard)
	if err != nil || c.Device || c.Addr != udplink.DefaultAddr {
		t.Fatalf("defaults %+v %v", c, err)
	}
	c, err = ParseFlags([]string{"--addr", "127.0.0.1:9000"}, io.Discard)
	if err != nil || c.Addr != "127.0.0.1:9000" {
		t.Fatalf("--addr %+v %v", c, err)
	}
	c, err = ParseFlags([]string{"--device"}, io.Discard)
	if err != nil || !c.Device || c.Addr != "192.168.4.1:52041" {
		t.Fatalf("--device %+v %v", c, err)
	}
	if _, err := ParseFlags([]string{"--device", "--addr", "127.0.0.1:1"}, io.Discard); err == nil {
		t.Fatal("--device with --addr accepted")
	}
	if _, err := ParseFlags([]string{"extra"}, io.Discard); err == nil {
		t.Fatal("an argument accepted")
	}
}

// failRunner fails the test on any command.
type failRunner struct{ t *testing.T }

func (f failRunner) Run(_ context.Context, name string, args ...string) ([]byte, error) {
	f.t.Errorf("ran %s %v off the device", name, args)
	return nil, errors.New("no")
}

func TestDesktopStartRunsNoWiFiStep(t *testing.T) {
	sys := System{
		Runner:   failRunner{t},
		Addrs:    func(string) ([]net.IP, error) { t.Error("address check off the device"); return nil, nil },
		ReadFile: func(string) ([]byte, error) { t.Error("signal read off the device"); return nil, os.ErrNotExist },
	}
	s, err := Start(context.Background(), Config{Addr: "127.0.0.1:9"}, sys)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	s.App.SampleSignal(context.Background())
}

// recRunner records commands and answers iw; with robot saved, it answers
// wpa_cli as a TrimUI with the robot's network saved as id 2 that
// associates at once, and udhcpc by running lease.
type recRunner struct {
	calls []string
	robot bool
	lease func()
}

func (r *recRunner) Run(_ context.Context, name string, args ...string) ([]byte, error) {
	cmd := strings.Join(append([]string{name}, args...), " ")
	r.calls = append(r.calls, cmd)
	switch {
	case cmd == "iw dev wlan0 get power_save":
		return []byte("Power save: on\n"), nil
	case !r.robot:
	case cmd == wpaCmd+"list_networks":
		return []byte("network id / ssid / bssid / flags\n0\tmeki-net\tany\t[CURRENT]\n2\tmini-spooder\tany\t\n"), nil
	case strings.HasPrefix(cmd, wpaCmd+"select_network"), cmd == wpaCmd+"enable_network all", cmd == wpaCmd+"reassociate":
		return []byte("OK\n"), nil
	case cmd == wpaCmd+"status":
		return []byte("ssid=mini-spooder\nwpa_state=COMPLETED\n"), nil
	case strings.HasPrefix(cmd, "udhcpc") && r.lease != nil:
		r.lease()
	}
	return nil, nil
}

const wpaCmd = "wpa_cli -p /etc/wifi/sockets -i wlan0 "

// joinAddrs is wlan0 on the home network until the lease moves it.
type joinAddrs struct{ ip string }

func (a *joinAddrs) addrs(string) ([]net.IP, error) { return []net.IP{net.ParseIP(a.ip)}, nil }

// Off the robot's network with it saved: Start joins it, and Close gives
// the other networks back after restoring power saving.
func TestDeviceStartJoinsAndCloseRestores(t *testing.T) {
	a := &joinAddrs{ip: "192.168.0.84"}
	run := &recRunner{robot: true, lease: func() { a.ip = "192.168.4.101" }}
	var last float64
	sys := System{Runner: run, Addrs: a.addrs, ReadFile: func(string) ([]byte, error) { return nil, os.ErrNotExist },
		Progress: func(f float64) { last = f }}
	s, err := Start(context.Background(), Config{Device: true, Addr: "127.0.0.1:9"}, sys)
	if err != nil {
		t.Fatal(err)
	}
	if !contains(run.calls, wpaCmd+"select_network 2") || last != 1 {
		t.Fatalf("ran %v (progress %v), want the robot's network selected", run.calls, last)
	}
	if contains(run.calls, wpaCmd+"enable_network all") {
		t.Fatalf("ran %v: networks given back while the run goes on", run.calls)
	}
	if err := s.Close(); err != nil {
		t.Fatal(err)
	}
	n := len(run.calls)
	want := []string{"iw dev wlan0 set power_save on", wpaCmd + "enable_network all", wpaCmd + "reassociate"}
	if n < 3 || strings.Join(run.calls[n-3:], "|") != strings.Join(want, "|") {
		t.Fatalf("ran %v, want %v at close", run.calls, want)
	}
	// A second Close (the signal backstop's) gives nothing back again.
	_ = s.Close()
	if contains(run.calls[n:], wpaCmd+"enable_network all") || contains(run.calls[n:], wpaCmd+"reassociate") {
		t.Fatalf("ran %v after a second close, want no second give-back", run.calls[n:])
	}
}

// A start that fails after the join gives the networks back.
func TestDeviceStartFailingAfterJoinRestores(t *testing.T) {
	a := &joinAddrs{ip: "192.168.0.84"}
	run := &recRunner{robot: true, lease: func() { a.ip = "192.168.4.101" }}
	sys := System{Runner: run, Addrs: a.addrs, ReadFile: os.ReadFile}
	if _, err := Start(context.Background(), Config{Device: true, Addr: "no port"}, sys); err == nil {
		t.Fatal("start with a bad address passed")
	}
	if !contains(run.calls, wpaCmd+"enable_network all") || !contains(run.calls, wpaCmd+"reassociate") {
		t.Fatalf("ran %v, want the networks given back", run.calls)
	}
}

// A failed join (no lease): the join message, the networks given back, and
// power saving untouched.
func TestDeviceStartJoinFails(t *testing.T) {
	a := &joinAddrs{ip: "192.168.0.84"}
	run := &recRunner{robot: true}
	sys := System{Runner: run, Addrs: a.addrs, ReadFile: os.ReadFile}
	_, err := Start(context.Background(), Config{Device: true, Addr: "127.0.0.1:9"}, sys)
	var nj *wlan.NotJoinedError
	if !errors.As(err, &nj) || !strings.Contains(JoinMessage(err), "mini-spooder") {
		t.Fatalf("got %v, want the join message", err)
	}
	if !contains(run.calls, wpaCmd+"reassociate") {
		t.Fatalf("ran %v, want the networks given back", run.calls)
	}
	if contains(run.calls, "iw dev wlan0 set power_save off") {
		t.Fatal("power save changed before the join passed")
	}
}

func addrs(ip string) wlan.Addrs {
	return func(string) ([]net.IP, error) { return []net.IP{net.ParseIP(ip)}, nil }
}

func TestDeviceStartJoined(t *testing.T) {
	run := &recRunner{}
	sys := System{Runner: run, Addrs: addrs("192.168.4.2"), ReadFile: func(string) ([]byte, error) { return nil, os.ErrNotExist }}
	s, err := Start(context.Background(), Config{Device: true, Addr: "127.0.0.1:9"}, sys)
	if err != nil {
		t.Fatal(err)
	}
	for _, c := range run.calls {
		if strings.HasPrefix(c, "udhcpc") || strings.HasPrefix(c, "wpa_cli") {
			t.Errorf("joined while on the robot's network: %v", run.calls)
		}
	}
	if !contains(run.calls, "iw dev wlan0 set power_save off") {
		t.Errorf("power save not turned off: %v", run.calls)
	}
	if err := s.Close(); err != nil {
		t.Fatal(err)
	}
	if run.calls[len(run.calls)-1] != "iw dev wlan0 set power_save on" {
		t.Errorf("power save not restored on close: %v", run.calls)
	}
}

func contains(xs []string, x string) bool {
	for _, v := range xs {
		if v == x {
			return true
		}
	}
	return false
}

// Robot's network not saved: the join message, and no socket, so no
// datagram.
func TestDeviceStartNotJoinedSendsNothing(t *testing.T) {
	robot, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer robot.Close()
	run := &recRunner{}
	sys := System{Runner: run, Addrs: addrs("10.0.0.5"), ReadFile: os.ReadFile}
	_, err = Start(context.Background(), Config{Device: true, Addr: robot.LocalAddr().String()}, sys)
	var nj *wlan.NotJoinedError
	if !errors.As(err, &nj) || !strings.Contains(err.Error(), "mini-spooder") || !strings.Contains(err.Error(), "87654321") {
		t.Fatalf("got %v, want the join message", err)
	}
	if contains(run.calls, "iw dev wlan0 set power_save off") {
		t.Error("power save changed before the check passed")
	}
	_ = robot.SetReadDeadline(time.Now().Add(100 * time.Millisecond))
	if n, _, err := robot.ReadFromUDP(make([]byte, 64)); err == nil {
		t.Fatalf("the robot received %d bytes", n)
	}
}
