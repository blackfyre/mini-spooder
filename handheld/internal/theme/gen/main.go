// Command gen writes theme_gen.go from tokens.json (go generate in
// internal/theme; see package themegen).
package main

import (
	"fmt"
	"os"

	"github.com/blackfyre/mini-spooder/handheld/internal/theme/themegen"
)

func main() {
	in, err := os.ReadFile("tokens.json")
	if err == nil {
		var out []byte
		if out, err = themegen.Generate(in); err == nil {
			err = os.WriteFile("theme_gen.go", out, 0o644)
		}
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "theme gen:", err)
		os.Exit(1)
	}
}
