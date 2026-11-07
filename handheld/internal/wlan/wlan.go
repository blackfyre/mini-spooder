// Package wlan is the pak's WiFi housekeeping on the device (openspec change
// handheld-pak, designs D6 and D7): the check that wlan0 is on the robot's
// network with one lease renewal, the join
// message, the power-saving setting read, turned off and restored, and the
// signal level. Every command runs through a Runner, so the host tests
// cover each branch with a fake one.
package wlan

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"net"
	"os/exec"
	"strconv"
	"strings"
)

// Interface is the device's WiFi interface.
const Interface = "wlan0"

// The robot's access point (drive-link: Access point; design A1).
const (
	RobotSSID     = "mini-spooder"
	RobotPassword = "87654321"
)

// RobotNet is the robot access point's network.
var RobotNet = net.IPNet{IP: net.IPv4(192, 168, 4, 0).To4(), Mask: net.CIDRMask(24, 32)}

// WirelessPath is the kernel's wireless statistics.
const WirelessPath = "/proc/net/wireless"

// Runner runs a command to completion and returns its standard output.
type Runner interface {
	Run(ctx context.Context, name string, args ...string) ([]byte, error)
}

// ExecRunner runs commands as processes; ctx's end kills one still
// running.
type ExecRunner struct{}

// Run runs name with args and waits for it.
func (ExecRunner) Run(ctx context.Context, name string, args ...string) ([]byte, error) {
	return exec.CommandContext(ctx, name, args...).Output()
}

// Addrs lists an interface's IP addresses.
type Addrs func(iface string) ([]net.IP, error)

// InterfaceAddrs is the Addrs of the running system.
func InterfaceAddrs(iface string) ([]net.IP, error) {
	ifi, err := net.InterfaceByName(iface)
	if err != nil {
		return nil, err
	}
	as, err := ifi.Addrs()
	if err != nil {
		return nil, err
	}
	var ips []net.IP
	for _, a := range as {
		if n, ok := a.(*net.IPNet); ok {
			ips = append(ips, n.IP)
		}
	}
	return ips, nil
}

// onRobot tells whether one of the interface's IPv4 addresses is in
// RobotNet. An interface that cannot be read holds none.
func onRobot(addrs Addrs) bool {
	ips, err := addrs(Interface)
	if err != nil {
		return false
	}
	for _, ip := range ips {
		if v4 := ip.To4(); v4 != nil && RobotNet.Contains(v4) {
			return true
		}
	}
	return false
}

// NotJoinedError is the address check's failure; its message is the join
// message shown to the user.
type NotJoinedError struct {
	// Network is the SSID wlan0 is on, or "" when it is on none.
	Network string
}

func (e *NotJoinedError) Error() string {
	var b strings.Builder
	fmt.Fprintf(&b, "Join the robot's network %q in NextUI's WiFi settings, with the password %s.",
		RobotSSID, RobotPassword)
	if e.Network != "" {
		fmt.Fprintf(&b, " %s is on %q now.", Interface, e.Network)
	} else {
		fmt.Fprintf(&b, " %s is on no network now.", Interface)
	}
	return b.String()
}

// RenewArgs is the one lease renewal: udhcpc in the foreground, quitting
// once it has a lease or after five tries.
var RenewArgs = []string{"-i", Interface, "-n", "-q", "-t", "5"}

// CheckAddress passes when wlan0 holds an IPv4 address on the robot's
// network. Otherwise it renews the lease once with udhcpc and checks again;
// still none, it returns a *NotJoinedError naming the network wlan0 is on.
func CheckAddress(ctx context.Context, run Runner, addrs Addrs) error {
	if onRobot(addrs) {
		return nil
	}
	// A failed renewal is not an error of its own: the second check says
	// whether the address is there.
	_, _ = run.Run(ctx, "udhcpc", RenewArgs...)
	if onRobot(addrs) {
		return nil
	}
	return &NotJoinedError{Network: CurrentNetwork(ctx, run)}
}

// CurrentNetwork is the SSID wlan0 is on (`iw dev wlan0 link`), or "".
func CurrentNetwork(ctx context.Context, run Runner) string {
	out, err := run.Run(ctx, "iw", "dev", Interface, "link")
	if err != nil {
		return ""
	}
	for _, line := range strings.Split(string(out), "\n") {
		if ssid, ok := strings.CutPrefix(strings.TrimSpace(line), "SSID:"); ok {
			return strings.TrimSpace(ssid)
		}
	}
	return ""
}

// PowerSave is wlan0's power-saving setting as read at the start, for
// turning it off and restoring it.
type PowerSave struct {
	run   Runner
	saved string // "on" or "off"
}

// ReadPowerSave reads the setting (`iw dev wlan0 get power_save`, which
// prints "Power save: on" or "... off").
func ReadPowerSave(ctx context.Context, run Runner) (*PowerSave, error) {
	out, err := run.Run(ctx, "iw", "dev", Interface, "get", "power_save")
	if err != nil {
		return nil, fmt.Errorf("reading %s power save: %w", Interface, err)
	}
	_, v, ok := strings.Cut(string(out), ":")
	v = strings.TrimSpace(v)
	if !ok || (v != "on" && v != "off") {
		return nil, fmt.Errorf("reading %s power save: unexpected %q", Interface, bytes.TrimSpace(out))
	}
	return &PowerSave{run: run, saved: v}, nil
}

// Saved is the setting read at the start.
func (p *PowerSave) Saved() string { return p.saved }

func (p *PowerSave) set(ctx context.Context, v string) error {
	if _, err := p.run.Run(ctx, "iw", "dev", Interface, "set", "power_save", v); err != nil {
		return fmt.Errorf("setting %s power save %s: %w", Interface, v, err)
	}
	return nil
}

// Off turns power saving off.
func (p *PowerSave) Off(ctx context.Context) error { return p.set(ctx, "off") }

// Restore sets the setting read at the start again. Restoring twice is
// harmless.
func (p *PowerSave) Restore(ctx context.Context) error { return p.set(ctx, p.saved) }

// ErrNoSignal is returned when the signal level is unknown.
var ErrNoSignal = errors.New("signal level unknown")

// ParseSignal reads wlan0's signal level in dBm from the contents of
// /proc/net/wireless: the level column of its line, as in
//
//	wlan0: 0000   70.  -40.  -256        0      0      0      0      0        0
func ParseSignal(b []byte) (int, error) {
	for _, line := range strings.Split(string(b), "\n") {
		rest, ok := strings.CutPrefix(strings.TrimSpace(line), Interface+":")
		if !ok {
			continue
		}
		fields := strings.Fields(rest)
		if len(fields) < 3 {
			return 0, ErrNoSignal
		}
		level, err := strconv.ParseFloat(strings.TrimSuffix(fields[2], "."), 64)
		if err != nil {
			return 0, ErrNoSignal
		}
		return int(level), nil
	}
	return 0, ErrNoSignal
}

// ParseLinkSignal reads the signal level in dBm from the output of
// `iw dev wlan0 link`, as in
//
//	Connected to 02:00:00:00:00:00 (on wlan0)
//		SSID: mini-spooder
//		signal: -40 dBm
//
// Not connected (no signal line) reads as ErrNoSignal.
func ParseLinkSignal(b []byte) (int, error) {
	for _, line := range strings.Split(string(b), "\n") {
		rest, ok := strings.CutPrefix(strings.TrimSpace(line), "signal:")
		if !ok {
			continue
		}
		fields := strings.Fields(rest)
		if len(fields) == 0 {
			return 0, ErrNoSignal
		}
		level, err := strconv.Atoi(fields[0])
		if err != nil {
			return 0, ErrNoSignal
		}
		return level, nil
	}
	return 0, ErrNoSignal
}

// Signal reads wlan0's signal level: from the kernel's wireless statistics
// through readFile, or, when they have no wlan0 line (cfg80211 drivers
// without wireless extensions, as on the TrimUI Smart Pro, have no file at
// all), from `iw dev wlan0 link` through run. Neither giving one reads as
// ErrNoSignal.
func Signal(ctx context.Context, run Runner, readFile func(string) ([]byte, error)) (int, error) {
	if b, err := readFile(WirelessPath); err == nil {
		if dbm, err := ParseSignal(b); err == nil {
			return dbm, nil
		}
	}
	out, err := run.Run(ctx, "iw", "dev", Interface, "link")
	if err != nil {
		return 0, ErrNoSignal
	}
	return ParseLinkSignal(out)
}
