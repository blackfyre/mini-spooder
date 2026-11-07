package theme

import (
	"bytes"
	"os"
	"strings"
	"testing"

	"github.com/blackfyre/mini-spooder/handheld/internal/theme/themegen"
)

// theme_gen.go is what the generator writes from the snapshot.
func TestColoursMatchTokens(t *testing.T) {
	in, err := os.ReadFile("tokens.json")
	if err != nil {
		t.Fatal(err)
	}
	want, err := themegen.Generate(in)
	if err != nil {
		t.Fatal(err)
	}
	got, err := os.ReadFile("theme_gen.go")
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, want) {
		t.Fatal("theme_gen.go differs from tokens.json: run go generate ./internal/theme")
	}
}

// A changed or missing token is caught.
func TestChangedOrMissingTokenFails(t *testing.T) {
	in, err := os.ReadFile("tokens.json")
	if err != nil {
		t.Fatal(err)
	}
	gen, err := os.ReadFile("theme_gen.go")
	if err != nil {
		t.Fatal(err)
	}
	changed := strings.Replace(string(in), `"light": "#7fd88a"`, `"light": "#7fd88b"`, 1)
	if changed == string(in) {
		t.Fatal("bf-ok's value not found in the snapshot")
	}
	out, err := themegen.Generate([]byte(changed))
	if err != nil || bytes.Equal(out, gen) {
		t.Fatalf("a changed bf-ok gives the same table (err %v)", err)
	}
	missing := strings.Replace(string(in), `"name": "bf-warn"`, `"name": "bf-warn-gone"`, 1)
	if _, err := themegen.Generate([]byte(missing)); err == nil {
		t.Fatal("a missing bf-warn generated a table")
	}
}

func TestKnownValues(t *testing.T) {
	// Spot checks against the design system's published values.
	if Ink != (Color{0x1b, 0x1b, 0x19}) || Surface != (Color{0xf3, 0xf2, 0xee}) || ShadowOutlineDark != (Color{0, 0, 0}) {
		t.Fatalf("ink %v surface %v outline %v", Ink, Surface, ShadowOutlineDark)
	}
}
