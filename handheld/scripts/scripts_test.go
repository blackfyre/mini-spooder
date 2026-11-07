package scripts

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// stubReadelf prints a controlled header and version needs.
func stubReadelf(machine, glibc string) string {
	return `#!/bin/sh
case "$1" in
-h) printf '%s\n' "ELF Header:" "  Class:                             ELF64" "  Machine:                           ` + machine + `" ;;
-V) printf '%s\n' "Version needs section '.gnu.version_r' contains 1 entry:" \
     " Addr: 0x0000000000000000  Offset: 0x000400  Link: 6 (.dynstr)" \
     "  000000: Version: 1  File: libc.so.6  Cnt: 2" \
     "  0x0010:   Name: GLIBC_2.17  Flags: none  Version: 3" \
     "  0x0020:   Name: ` + glibc + `  Flags: none  Version: 2" ;;
*) exit 2 ;;
esac
`
}

func runCheckGlibc(t *testing.T, machine, glibc string) (string, error) {
	t.Helper()
	bin := t.TempDir()
	if err := os.WriteFile(filepath.Join(bin, "readelf"), []byte(stubReadelf(machine, glibc)), 0o755); err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command("sh", "check-glibc.sh", "minispooder")
	cmd.Env = append(os.Environ(), "PATH="+bin+":"+os.Getenv("PATH"))
	out, err := cmd.CombinedOutput()
	return string(out), err
}

func TestCheckGlibc(t *testing.T) {
	for _, c := range []struct {
		machine, glibc string
		pass           bool
	}{
		{"AArch64", "GLIBC_2.31", true},
		{"AArch64", "GLIBC_2.34", false},
		// 2.4 < 2.31: the versions compare as numbers, not as text.
		{"AArch64", "GLIBC_2.4", true},
		{"Advanced Micro Devices X86-64", "GLIBC_2.17", false},
	} {
		out, err := runCheckGlibc(t, c.machine, c.glibc)
		if (err == nil) != c.pass {
			t.Errorf("%s with %s: err %v, want pass %v\n%s", c.machine, c.glibc, err, c.pass, out)
		}
	}
}

func TestCheckGoVersion(t *testing.T) {
	dir := t.TempDir()
	write := func(name, body string) string {
		p := filepath.Join(dir, name)
		if err := os.WriteFile(p, []byte(body), 0o644); err != nil {
			t.Fatal(err)
		}
		return p
	}
	mise := write("mise.toml", "[tools]\ngo = \"1.27.1\"\n")
	for _, c := range []struct {
		arg  string
		pass bool
	}{
		{"ARG GO_VERSION=1.27.1", true},
		{"ARG GO_VERSION=1.27.0", false},
		{"ARG GO_VERSIONS=1.27.1", false},
	} {
		cf := write("Containerfile", "FROM x\n"+c.arg+"\n")
		out, err := exec.Command("sh", "check-go-version.sh", cf, mise).CombinedOutput()
		if (err == nil) != c.pass {
			t.Errorf("%q: err %v, want pass %v\n%s", c.arg, err, c.pass, out)
		}
	}
}

// The repository's own Containerfile and mise.toml agree.
func TestRepositoryGoVersionsAgree(t *testing.T) {
	out, err := exec.Command("sh", "check-go-version.sh", "../build/Containerfile", "../../mise.toml").CombinedOutput()
	if err != nil || !strings.Contains(string(out), "Go ") {
		t.Fatalf("check-go-version: %v\n%s", err, out)
	}
}

// check-deps.sh names the missing package when pkg-config lacks an entry.
func TestCheckDepsNamesMissingGfx(t *testing.T) {
	bin := t.TempDir()
	stub := "#!/bin/sh\n[ \"$1\" = --exists ] || exit 2\n[ \"$2\" = SDL2_gfx ] && exit 1\nexit 0\n"
	if err := os.WriteFile(filepath.Join(bin, "pkg-config"), []byte(stub), 0o755); err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command("sh", "check-deps.sh")
	cmd.Env = append(os.Environ(), "PATH="+bin+":"+os.Getenv("PATH"))
	out, err := cmd.CombinedOutput()
	if err == nil {
		t.Fatalf("check-deps passed with SDL2_gfx missing\n%s", out)
	}
	if !strings.Contains(string(out), "SDL2_gfx-devel") && !strings.Contains(string(out), "libsdl2-gfx-dev") {
		t.Errorf("check-deps did not name the SDL2_gfx package:\n%s", out)
	}
	if strings.Contains(string(out), "SDL2_ttf") {
		t.Errorf("check-deps named SDL2_ttf, which is present:\n%s", out)
	}
}

// packageRoot lays out a repository root for package.sh: the scripts, the
// launch script and a device build whose binary readelf (stubbed) reports
// as machine with glibc.
func packageRoot(t *testing.T, machine, glibc string) (root string, env []string) {
	t.Helper()
	root = t.TempDir()
	bin := filepath.Join(root, "stub")
	for _, d := range []string{"handheld/scripts", "handheld/pak", "build/handheld/arm64/lib", "stub"} {
		if err := os.MkdirAll(filepath.Join(root, d), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	copyFile := func(from, to string) {
		b, err := os.ReadFile(from)
		if err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(root, to), b, 0o755); err != nil {
			t.Fatal(err)
		}
	}
	copyFile("package.sh", "handheld/scripts/package.sh")
	copyFile("check-glibc.sh", "handheld/scripts/check-glibc.sh")
	copyFile("../pak/launch.sh", "handheld/pak/launch.sh")
	for _, f := range []string{"build/handheld/arm64/minispooder", "build/handheld/arm64/lib/libSDL2_gfx-1.0.so.0"} {
		if err := os.WriteFile(filepath.Join(root, f), []byte("x"), 0o600); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.WriteFile(filepath.Join(bin, "readelf"), []byte(stubReadelf(machine, glibc)), 0o755); err != nil {
		t.Fatal(err)
	}
	return root, append(os.Environ(), "PATH="+bin+":"+os.Getenv("PATH"))
}

func TestPackageZip(t *testing.T) {
	if _, err := exec.LookPath("zip"); err != nil {
		t.Skip("zip is not installed")
	}
	root, env := packageRoot(t, "AArch64", "GLIBC_2.31")
	cmd := exec.Command("sh", "handheld/scripts/package.sh")
	cmd.Dir, cmd.Env = root, env
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("package.sh: %v\n%s", err, out)
	}
	out, err := exec.Command("unzip", "-Z", "-1", filepath.Join(root, "build/handheld/MiniSpooder.pak.zip")).Output()
	if err != nil {
		t.Fatal(err)
	}
	want := "MiniSpooder.pak/launch.sh\nMiniSpooder.pak/minispooder\nMiniSpooder.pak/lib/libSDL2_gfx-1.0.so.0"
	if got := strings.TrimSpace(string(out)); got != want {
		t.Errorf("zip entries:\n%s\nwant:\n%s", got, want)
	}
	modes, err := exec.Command("unzip", "-Z", "-l", filepath.Join(root, "build/handheld/MiniSpooder.pak.zip")).Output()
	if err != nil {
		t.Fatal(err)
	}
	for _, line := range strings.Split(string(modes), "\n") {
		if strings.HasSuffix(line, "launch.sh") || strings.HasSuffix(line, "/minispooder") {
			if !strings.HasPrefix(line, "-rwxr-xr-x") {
				t.Errorf("not mode 755: %s", line)
			}
		}
	}
}

func TestPackageRefusesWrongBinary(t *testing.T) {
	for _, c := range []struct{ machine, glibc string }{
		{"AArch64", "GLIBC_2.34"},
		{"Advanced Micro Devices X86-64", "GLIBC_2.17"},
	} {
		root, env := packageRoot(t, c.machine, c.glibc)
		cmd := exec.Command("sh", "handheld/scripts/package.sh")
		cmd.Dir, cmd.Env = root, env
		if out, err := cmd.CombinedOutput(); err == nil {
			t.Errorf("%s with %s: package.sh passed\n%s", c.machine, c.glibc, out)
		}
		if _, err := os.Stat(filepath.Join(root, "build/handheld/MiniSpooder.pak.zip")); err == nil {
			t.Errorf("%s with %s: a zip was written", c.machine, c.glibc)
		}
	}
}
