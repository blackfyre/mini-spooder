// Package wlan is the pak's WiFi housekeeping on the device (openspec change
// handheld-pak, designs D6 and D7): the join of the robot's saved network
// through wpa_cli and udhcpc, with the other networks given back afterwards,
// the join message, the power-saving setting read, turned off and restored,
// and the signal level. Every command runs through a Runner, so the host tests
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
	"sync"
	"time"
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

// NotJoinedError is the join's failure; its message is the join message
// shown to the user.
type NotJoinedError struct {
	// Problem is the step of the join that failed, or "" when the robot's
	// network is not saved on the device.
	Problem string
	// Network is the SSID wlan0 is on, or "" when it is on none.
	Network string
}

func (e *NotJoinedError) Error() string {
	var b strings.Builder
	if e.Problem == "" {
		fmt.Fprintf(&b, "Join the robot's network %q once in NextUI's WiFi settings, with the password %s, so that it is saved: the pak then joins it by itself.",
			RobotSSID, RobotPassword)
		if e.Network != "" {
			fmt.Fprintf(&b, " %s is on %q now.", Interface, e.Network)
		} else {
			fmt.Fprintf(&b, " %s is on no network now.", Interface)
		}
		return b.String()
	}
	fmt.Fprintf(&b, "Could not join the robot's network %q: %s. Check that the robot is on and near, then retry.",
		RobotSSID, e.Problem)
	return b.String()
}

// RenewArgs is the lease request: udhcpc in the foreground, quitting once
// it has a lease or after five tries.
var RenewArgs = []string{"-i", Interface, "-n", "-q", "-t", "5"}

// WPASockets is the TrimUI's wpa_supplicant control socket directory.
const WPASockets = "/etc/wifi/sockets"

// wpaArgs is a wpa_cli command on wlan0.
func wpaArgs(cmd ...string) []string {
	return append([]string{"-p", WPASockets, "-i", Interface}, cmd...)
}

// The join's bounds; the tests shorten them.
var (
	// AssociateWithin bounds the wait for wpa_state=COMPLETED on the
	// robot's network after select_network.
	AssociateWithin = 10 * time.Second
	// LeaseWithin bounds the lease request.
	LeaseWithin = 20 * time.Second
	// RestoreWithin bounds giving the networks back.
	RestoreWithin = 5 * time.Second
	pollEvery     = 250 * time.Millisecond
)

// Pin is the run's hold on the robot's network: select_network has
// disabled every other saved network, so NextUI cannot move wlan0 away.
// Nothing is saved to the supplicant's configuration.
type Pin struct {
	run  Runner
	once sync.Once
	err  error
}

// Restore enables every saved network again and reassociates, so the
// device returns to its usual networks. Only the first call acts.
func (p *Pin) Restore() error {
	p.once.Do(func() {
		ctx, cancel := context.WithTimeout(context.Background(), RestoreWithin)
		defer cancel()
		var errs []error
		for _, cmd := range []string{"enable_network all", "reassociate"} {
			if _, err := p.run.Run(ctx, "wpa_cli", wpaArgs(strings.Fields(cmd)...)...); err != nil {
				errs = append(errs, fmt.Errorf("wpa_cli %s: %w", cmd, err))
			}
		}
		p.err = errors.Join(errs...)
	})
	return p.err
}

// SavedNetwork is the id of the saved network named ssid in the output of
// `wpa_cli list_networks` (a header, then "id<TAB>ssid<TAB>bssid<TAB>flags"
// lines), or "" when there is none.
func SavedNetwork(listing []byte, ssid string) string {
	lines := strings.Split(string(listing), "\n")
	for _, line := range lines {
		f := strings.Split(strings.TrimRight(line, "\r"), "\t")
		if len(f) < 2 || f[1] != ssid {
			continue
		}
		if _, err := strconv.Atoi(f[0]); err == nil {
			return f[0]
		}
	}
	return ""
}

// associated tells whether `wpa_cli status` shows wlan0 associated with the
// robot's network.
func associated(status []byte) bool {
	var completed, robot bool
	for _, line := range strings.Split(string(status), "\n") {
		k, v, _ := strings.Cut(strings.TrimSpace(line), "=")
		switch k {
		case "wpa_state":
			completed = v == "COMPLETED"
		case "ssid":
			robot = v == RobotSSID
		}
	}
	return completed && robot
}

// Join brings wlan0 onto the robot's network. When it already holds an
// IPv4 address there, Join runs nothing and returns a nil Pin. Otherwise it
// finds the saved network named RobotSSID (none: a *NotJoinedError with no
// Problem, nothing changed), selects it, which disables the other saved
// networks, waits up to AssociateWithin for the association, requests a
// lease with udhcpc (bounded by LeaseWithin) and checks that the address is
// in RobotNet. Each failure after the selection restores the networks and
// returns a *NotJoinedError naming the step; ctx's end restores them too.
// On success the Pin holds the robot's network until its Restore. progress,
// if not nil, gets the share of the join done, from 0 to 1.
func Join(ctx context.Context, run Runner, addrs Addrs, progress func(float64)) (*Pin, error) {
	report := func(f float64) {
		if progress != nil {
			progress(f)
		}
	}
	if onRobot(addrs) {
		report(1)
		return nil, nil
	}
	listing, err := run.Run(ctx, "wpa_cli", wpaArgs("list_networks")...)
	if err != nil {
		if ctx.Err() != nil {
			return nil, ctx.Err()
		}
		return nil, &NotJoinedError{Problem: fmt.Sprintf("the WiFi supplicant does not answer (%v)", err)}
	}
	id := SavedNetwork(listing, RobotSSID)
	if id == "" {
		return nil, &NotJoinedError{Network: CurrentNetwork(ctx, run)}
	}
	report(0.1)
	pin := &Pin{run: run}
	fail := func(problem string) (*Pin, error) {
		rerr := pin.Restore()
		if ctx.Err() != nil {
			return nil, errors.Join(ctx.Err(), rerr)
		}
		return nil, errors.Join(&NotJoinedError{Problem: problem}, rerr)
	}
	out, err := run.Run(ctx, "wpa_cli", wpaArgs("select_network", id)...)
	if err != nil || strings.TrimSpace(string(out)) != "OK" {
		return fail(fmt.Sprintf("selecting it failed (%s)", failure(out, err)))
	}
	report(0.2)

	actx, cancel := context.WithTimeout(ctx, AssociateWithin)
	defer cancel()
	tick := time.NewTicker(pollEvery)
	defer tick.Stop()
	start := time.Now()
	for {
		status, err := run.Run(actx, "wpa_cli", wpaArgs("status")...)
		if err == nil && associated(status) {
			break
		}
		select {
		case <-actx.Done():
			return fail(fmt.Sprintf("it did not connect within %v", AssociateWithin))
		case <-tick.C:
			report(0.2 + 0.4*min(1, float64(time.Since(start))/float64(AssociateWithin)))
		}
	}
	report(0.6)

	lctx, lcancel := context.WithTimeout(ctx, LeaseWithin)
	defer lcancel()
	_, err = run.Run(lctx, "udhcpc", RenewArgs...)
	report(0.9)
	if onRobot(addrs) {
		report(1)
		return pin, nil
	}
	if ctx.Err() != nil {
		return fail("")
	}
	if err != nil {
		return fail(fmt.Sprintf("its DHCP server gave no address (udhcpc: %v)", err))
	}
	return fail(fmt.Sprintf("the address it gave is not in %s (%s holds %s)", RobotNet.String(), Interface, ipv4s(addrs)))
}

// failure describes a wpa_cli command that failed or did not answer OK.
func failure(out []byte, err error) string {
	if err != nil {
		return err.Error()
	}
	if s := strings.TrimSpace(string(out)); s != "" {
		return s
	}
	return "no answer"
}

// ipv4s lists wlan0's IPv4 addresses for a message.
func ipv4s(addrs Addrs) string {
	ips, _ := addrs(Interface)
	var s []string
	for _, ip := range ips {
		if v4 := ip.To4(); v4 != nil {
			s = append(s, v4.String())
		}
	}
	if len(s) == 0 {
		return "no IPv4 address"
	}
	return strings.Join(s, ", ")
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
