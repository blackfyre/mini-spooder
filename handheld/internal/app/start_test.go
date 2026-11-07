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

// recRunner records commands and answers iw.
type recRunner struct{ calls []string }

func (r *recRunner) Run(_ context.Context, name string, args ...string) ([]byte, error) {
	cmd := strings.Join(append([]string{name}, args...), " ")
	r.calls = append(r.calls, cmd)
	if cmd == "iw dev wlan0 get power_save" {
		return []byte("Power save: on\n"), nil
	}
	return nil, nil
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
		if strings.HasPrefix(c, "udhcpc") {
			t.Errorf("renewed the lease while joined: %v", run.calls)
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

// Not joined: the join message, and no socket, so no datagram.
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
