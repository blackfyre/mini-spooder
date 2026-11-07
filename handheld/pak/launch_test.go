package pak

import (
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"testing"
	"time"
)

// stubIW is iw on PATH: it keeps wlan0's power-saving setting in $IW_STATE
// and logs every set to $IW_LOG.
const stubIW = `#!/bin/sh
case "$*" in
"dev wlan0 get power_save") echo "Power save: $(cat "$IW_STATE")" ;;
"dev wlan0 set power_save "*) echo "$5" > "$IW_STATE"; echo "set $5" >> "$IW_LOG" ;;
*) exit 1 ;;
esac
`

// stubWPA is wpa_cli on PATH: it keeps whether the other saved networks
// are disabled in $WPA_STATE ("enabled" or "disabled") and logs every
// enable_network and reassociate to $WPA_LOG.
const stubWPA = `#!/bin/sh
[ "$1 $2 $3 $4" = "-p /etc/wifi/sockets -i wlan0" ] || exit 1
shift 4
case "$*" in
list_networks)
  printf 'network id / ssid / bssid / flags\n'
  if [ "$(cat "$WPA_STATE")" = disabled ]; then
    printf '0\tmeki-net\tany\t[DISABLED]\n6\tmini-spooder\tany\t[CURRENT]\n'
  else
    printf '0\tmeki-net\tany\t[CURRENT]\n6\tmini-spooder\tany\t\n'
  fi ;;
"select_network "*) echo disabled > "$WPA_STATE"; echo OK ;;
"enable_network all") echo enabled > "$WPA_STATE"; echo enable >> "$WPA_LOG"; echo OK ;;
reassociate) echo reassociate >> "$WPA_LOG"; echo OK ;;
*) exit 1 ;;
esac
`

// pakDir makes a pak with launch.sh and a stub binary, and a bin directory
// with the stub iw, power saving on, and the stub wpa_cli, every network
// enabled.
func pakDir(t *testing.T, binary string) (dir string, env []string) {
	t.Helper()
	dir = t.TempDir()
	script, err := os.ReadFile("launch.sh")
	if err != nil {
		t.Fatal(err)
	}
	bin := filepath.Join(dir, "bin")
	pak := filepath.Join(dir, "MiniSpooder.pak")
	for _, d := range []string{bin, pak} {
		if err := os.Mkdir(d, 0o755); err != nil {
			t.Fatal(err)
		}
	}
	write := func(path, body string) {
		if err := os.WriteFile(path, []byte(body), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	write(filepath.Join(pak, "launch.sh"), string(script))
	write(filepath.Join(pak, "minispooder"), binary)
	write(filepath.Join(bin, "iw"), stubIW)
	write(filepath.Join(bin, "wpa_cli"), stubWPA)
	write(filepath.Join(dir, "wpa"), "enabled\n")
	realSleep, err := exec.LookPath("sleep")
	if err != nil {
		t.Fatal(err)
	}
	write(filepath.Join(bin, "sleep"), "#!/bin/sh\necho $$ >> \"$SLEEPS\"\nexec "+realSleep+" \"$@\"\n")
	write(filepath.Join(dir, "state"), "on\n")
	env = append(os.Environ(),
		"PATH="+bin+":"+os.Getenv("PATH"),
		"IW_STATE="+filepath.Join(dir, "state"),
		"IW_LOG="+filepath.Join(dir, "iw.log"),
		"WPA_STATE="+filepath.Join(dir, "wpa"),
		"WPA_LOG="+filepath.Join(dir, "wpa.log"),
		"MARK="+filepath.Join(dir, "running"),
		"ARGS="+filepath.Join(dir, "args"),
		"GRAND="+filepath.Join(dir, "grandchild"),
		"SLEEPS="+filepath.Join(dir, "sleeps"),
		"LOGS_PATH=",
	)
	return dir, env
}

// launch runs the pak's launch.sh with sh, or with the shell that
// $MINISPOODER_LAUNCH_SHELL names (such as "dash" or "busybox sh"), to check it
// under the shells it meets: dash on Debian and Ubuntu, BusyBox ash on the
// device, bash as sh on Fedora.
func launch(dir string) *exec.Cmd {
	shell := strings.Fields(os.Getenv("MINISPOODER_LAUNCH_SHELL"))
	if len(shell) == 0 {
		shell = []string{"sh"}
	}
	args := append(shell[1:], filepath.Join(dir, "MiniSpooder.pak", "launch.sh"))
	return exec.Command(shell[0], args...)
}

func read(t *testing.T, path string) string {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	return strings.TrimSpace(string(b))
}

// running tells whether pid still runs. A process that has ended but is
// not reaped yet (a zombie, which an init or subreaper slow to reap leaves
// for a while) still answers signal 0, so on Linux its state in /proc
// decides.
func running(pid int) bool {
	if syscall.Kill(pid, 0) != nil {
		return false
	}
	stat, err := os.ReadFile("/proc/" + strconv.Itoa(pid) + "/stat")
	if os.IsNotExist(err) {
		// Gone since the signal, or no /proc to ask.
		_, noProc := os.Stat("/proc/self")
		return noProc != nil
	}
	if err != nil {
		// Unreadable (hidepid, say): it answered signal 0, so it runs.
		return true
	}
	// The state follows the command name, which is in parentheses.
	i := strings.LastIndexByte(string(stat), ')')
	return i < 0 || i+2 >= len(stat) || stat[i+2] != 'Z'
}

// sleepsLeft lists the pids of the sleeps started under the pak (the
// stub sleep logs each to $SLEEPS) that still run a second after the script
// ended: the watchdog's, the binary's or its children's.
func sleepsLeft(t *testing.T, dir string) []int {
	t.Helper()
	b, err := os.ReadFile(filepath.Join(dir, "sleeps"))
	if os.IsNotExist(err) {
		return nil
	}
	if err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(time.Second)
	var left []int
	for _, f := range strings.Fields(string(b)) {
		pid, err := strconv.Atoi(f)
		if err != nil {
			t.Fatal(err)
		}
		for running(pid) && time.Now().Before(deadline) {
			time.Sleep(20 * time.Millisecond)
		}
		if running(pid) {
			left = append(left, pid)
			_ = syscall.Kill(pid, syscall.SIGKILL)
		}
	}
	return left
}

// waitFor waits until path exists.
func waitFor(t *testing.T, path string) {
	t.Helper()
	deadline := time.Now().Add(5 * time.Second)
	for {
		if _, err := os.Stat(path); err == nil {
			return
		}
		if time.Now().After(deadline) {
			t.Fatalf("%s did not appear", path)
		}
		time.Sleep(10 * time.Millisecond)
	}
}

func TestLaunchRestoresAfterBinaryFails(t *testing.T) {
	dir, env := pakDir(t, `#!/bin/sh
echo "$@" > "$ARGS"
iw dev wlan0 set power_save off
exit 1
`)
	cmd := launch(dir)
	cmd.Env = append(env, "STOP_GRACE=30")
	err := cmd.Run()
	if ee, ok := err.(*exec.ExitError); !ok || ee.ExitCode() != 1 {
		t.Fatalf("launch.sh: %v, want exit status 1", err)
	}
	if got := read(t, filepath.Join(dir, "args")); got != "--device" {
		t.Errorf("binary run with %q, want --device", got)
	}
	if got := read(t, filepath.Join(dir, "state")); got != "on" {
		t.Errorf("power save %q after the binary failed, want on", got)
	}
	if got := read(t, filepath.Join(dir, "iw.log")); got != "set off\nset on" {
		t.Errorf("iw sets %q, want off then on", got)
	}
	if left := sleepsLeft(t, dir); left != nil {
		t.Errorf("the watchdog's sleep %v outlived the script", left)
	}
}

// The binary turns power saving off, marks itself running and waits; it
// is then killed with SIGTERM, directly or through the script.
const waitingBinary = `#!/bin/sh
iw dev wlan0 set power_save off
sleep 30 &
echo $! > "$GRAND"
echo $$ > "$MARK"
exec sleep 30
`

func TestLaunchRestoresAfterBinaryKilled(t *testing.T) {
	dir, env := pakDir(t, waitingBinary)
	cmd := launch(dir)
	cmd.Env = env
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	waitFor(t, filepath.Join(dir, "running"))
	pid, err := strconv.Atoi(read(t, filepath.Join(dir, "running")))
	if err != nil {
		t.Fatal(err)
	}
	if err := syscall.Kill(pid, syscall.SIGTERM); err != nil {
		t.Fatal(err)
	}
	if err := cmd.Wait(); err == nil {
		t.Error("launch.sh exited 0 after the binary was killed")
	}
	if got := read(t, filepath.Join(dir, "state")); got != "on" {
		t.Errorf("power save %q after the binary was killed, want on", got)
	}
}

func TestLaunchRestoresAndStopsBinaryOnSIGTERM(t *testing.T) {
	dir, env := pakDir(t, waitingBinary)
	cmd := launch(dir)
	cmd.Env = env
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	waitFor(t, filepath.Join(dir, "running"))
	pid, err := strconv.Atoi(read(t, filepath.Join(dir, "running")))
	if err != nil {
		t.Fatal(err)
	}
	if err := cmd.Process.Signal(syscall.SIGTERM); err != nil {
		t.Fatal(err)
	}
	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()
	select {
	case err := <-done:
		if ee, ok := err.(*exec.ExitError); !ok || ee.ExitCode() != 143 {
			t.Errorf("launch.sh: %v, want exit status 143", err)
		}
	case <-time.After(5 * time.Second):
		_ = cmd.Process.Kill()
		t.Fatal("launch.sh did not exit on SIGTERM")
	}
	if got := read(t, filepath.Join(dir, "state")); got != "on" {
		t.Errorf("power save %q after SIGTERM, want on", got)
	}
	if err := syscall.Kill(pid, 0); err == nil {
		_ = syscall.Kill(pid, syscall.SIGKILL)
		t.Error("the binary is still running after launch.sh's SIGTERM")
	}
}

func TestLaunchLeavesUnreadableSettingAlone(t *testing.T) {
	dir, env := pakDir(t, `#!/bin/sh
exit 0
`)
	if err := os.WriteFile(filepath.Join(dir, "state"), []byte("unknown\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	cmd := launch(dir)
	cmd.Env = env
	if err := cmd.Run(); err != nil {
		t.Fatalf("launch.sh: %v", err)
	}
	if _, err := os.Stat(filepath.Join(dir, "iw.log")); err == nil {
		t.Errorf("iw set %q with an unreadable setting, want nothing", read(t, filepath.Join(dir, "iw.log")))
	}
}

// alive tells whether the process with the pid in path still runs.
func alive(t *testing.T, path string) bool {
	t.Helper()
	pid, err := strconv.Atoi(read(t, path))
	if err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		if !running(pid) {
			return false
		}
		time.Sleep(20 * time.Millisecond)
	}
	_ = syscall.Kill(pid, syscall.SIGKILL)
	return true
}

// A binary that crashes while a process it started still runs: launch.sh
// stops that process too.
func TestLaunchStopsWhatACrashedBinaryStarted(t *testing.T) {
	dir, env := pakDir(t, `#!/bin/sh
iw dev wlan0 set power_save off
sleep 30 &
echo $! > "$GRAND"
kill -SEGV $$
`)
	cmd := launch(dir)
	cmd.Env = env
	if err := cmd.Run(); err == nil {
		t.Error("launch.sh exited 0 after the binary crashed")
	}
	if got := read(t, filepath.Join(dir, "state")); got != "on" {
		t.Errorf("power save %q after the crash, want on", got)
	}
	if alive(t, filepath.Join(dir, "grandchild")) {
		t.Error("the crashed binary's child still runs after launch.sh exited")
	}
}

func TestLaunchStopsBinaryChildrenOnSIGTERM(t *testing.T) {
	dir, env := pakDir(t, waitingBinary)
	cmd := launch(dir)
	cmd.Env = env
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	waitFor(t, filepath.Join(dir, "running"))
	if err := cmd.Process.Signal(syscall.SIGTERM); err != nil {
		t.Fatal(err)
	}
	_ = cmd.Wait()
	if alive(t, filepath.Join(dir, "grandchild")) {
		t.Error("the binary's child still runs after launch.sh's SIGTERM")
	}
}

// A binary that ignores SIGTERM is killed after the grace period, and the
// setting is restored.
func TestLaunchKillsBinaryIgnoringSIGTERM(t *testing.T) {
	dir, env := pakDir(t, `#!/bin/sh
trap '' TERM
iw dev wlan0 set power_save off
echo $$ > "$MARK"
while :; do sleep 1; done
`)
	cmd := launch(dir)
	cmd.Env = append(env, "STOP_GRACE=0.3")
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	waitFor(t, filepath.Join(dir, "running"))
	if err := cmd.Process.Signal(syscall.SIGTERM); err != nil {
		t.Fatal(err)
	}
	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()
	select {
	case <-done:
	case <-time.After(5 * time.Second):
		_ = cmd.Process.Kill()
		t.Fatal("launch.sh did not exit with a binary ignoring SIGTERM")
	}
	if got := read(t, filepath.Join(dir, "state")); got != "on" {
		t.Errorf("power save %q, want on", got)
	}
	if alive(t, filepath.Join(dir, "running")) {
		t.Error("the binary ignoring SIGTERM still runs")
	}
}

// A binary that exits on SIGTERM ends the script at once, the watchdog
// included: a caller reading the script's output sees its end well before
// the grace period.
func TestLaunchEndsPromptlyOnSIGTERM(t *testing.T) {
	dir, env := pakDir(t, waitingBinary)
	cmd := launch(dir)
	cmd.Env = append(env, "STOP_GRACE=5")
	out, err := cmd.StdoutPipe()
	if err != nil {
		t.Fatal(err)
	}
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	waitFor(t, filepath.Join(dir, "running"))
	start := time.Now()
	if err := cmd.Process.Signal(syscall.SIGTERM); err != nil {
		t.Fatal(err)
	}
	eof := make(chan struct{})
	go func() {
		buf := make([]byte, 256)
		for {
			if _, err := out.Read(buf); err != nil {
				close(eof)
				return
			}
		}
	}()
	select {
	case <-eof:
	case <-time.After(4 * time.Second):
		_ = cmd.Process.Kill()
		t.Fatal("the script's output stayed open after SIGTERM")
	}
	_ = cmd.Wait()
	if d := time.Since(start); d > 2*time.Second {
		t.Errorf("output closed %v after SIGTERM, want well under the 5 s grace", d)
	}
	if left := sleepsLeft(t, dir); left != nil {
		t.Errorf("sleeps %v outlived the script", left)
	}
}

// A normal exit restores the setting, and with NextUI's LOGS_PATH the
// output goes to MiniSpooder.txt in it.
func TestLaunchRestoresAfterNormalExitAndLogs(t *testing.T) {
	dir, env := pakDir(t, `#!/bin/sh
iw dev wlan0 set power_save off
echo "binary ran"
exit 0
`)
	logs := filepath.Join(dir, "logs")
	if err := os.Mkdir(logs, 0o755); err != nil {
		t.Fatal(err)
	}
	cmd := launch(dir)
	cmd.Env = append(env, "LOGS_PATH="+logs)
	if err := cmd.Run(); err != nil {
		t.Fatalf("launch.sh: %v", err)
	}
	if got := read(t, filepath.Join(dir, "state")); got != "on" {
		t.Errorf("power save %q after a normal exit, want on", got)
	}
	if got := read(t, filepath.Join(logs, "MiniSpooder.txt")); !strings.Contains(got, "binary ran") {
		t.Errorf("MiniSpooder.txt holds %q, want the binary's output", got)
	}
}

// The library path puts the device's libraries first and the pak's lib/
// after them.
func TestLaunchLibraryPath(t *testing.T) {
	dir, env := pakDir(t, `#!/bin/sh
echo "$LD_LIBRARY_PATH" > "$ARGS"
`)
	cmd := launch(dir)
	cmd.Env = append(env, "LD_LIBRARY_PATH=")
	if err := cmd.Run(); err != nil {
		t.Fatalf("launch.sh: %v", err)
	}
	want := "/usr/trimui/lib:/usr/lib:" + filepath.Join(dir, "MiniSpooder.pak", "lib")
	if got := read(t, filepath.Join(dir, "args")); got != want {
		t.Errorf("LD_LIBRARY_PATH %q, want %q", got, want)
	}
}

// The binary selects the robot's network (disabling the others) and then
// crashes or is killed before giving them back: launch.sh gives them back.
func TestLaunchGivesNetworksBack(t *testing.T) {
	for name, binary := range map[string]string{
		"crash": `#!/bin/sh
wpa_cli -p /etc/wifi/sockets -i wlan0 select_network 6 >/dev/null
kill -SEGV $$
`,
		"SIGTERM": `#!/bin/sh
wpa_cli -p /etc/wifi/sockets -i wlan0 select_network 6 >/dev/null
echo $$ > "$MARK"
exec sleep 30
`,
	} {
		t.Run(name, func(t *testing.T) {
			dir, env := pakDir(t, binary)
			cmd := launch(dir)
			cmd.Env = env
			if err := cmd.Start(); err != nil {
				t.Fatal(err)
			}
			if name == "SIGTERM" {
				waitFor(t, filepath.Join(dir, "running"))
				if err := cmd.Process.Signal(syscall.SIGTERM); err != nil {
					t.Fatal(err)
				}
			}
			_ = cmd.Wait()
			if got := read(t, filepath.Join(dir, "wpa")); got != "enabled" {
				t.Errorf("networks %q after the binary ended, want enabled", got)
			}
			if got := read(t, filepath.Join(dir, "wpa.log")); got != "enable\nreassociate" {
				t.Errorf("wpa_cli ran %q, want enable_network all then reassociate", got)
			}
		})
	}
}

// A run that left every network enabled (the binary gave them back, or
// never joined) is not reassociated again.
func TestLaunchLeavesEnabledNetworksAlone(t *testing.T) {
	dir, env := pakDir(t, `#!/bin/sh
exit 0
`)
	cmd := launch(dir)
	cmd.Env = env
	if err := cmd.Run(); err != nil {
		t.Fatalf("launch.sh: %v", err)
	}
	if _, err := os.Stat(filepath.Join(dir, "wpa.log")); err == nil {
		t.Errorf("wpa_cli ran %q with every network enabled, want nothing", read(t, filepath.Join(dir, "wpa.log")))
	}
}
