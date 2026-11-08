package main

import (
	"slices"
	"testing"
)

func TestWrap(t *testing.T) {
	measure := func(s string) int32 { return int32(len(s)) }
	for _, c := range []struct {
		in    string
		width int32
		want  []string
	}{
		{"join the robot's network", 12, []string{"join the", "robot's", "network"}},
		{"a b c", 5, []string{"a b c"}},
		{"one\ntwo  three", 20, []string{"one", "two three"}},
		{"unbreakableword x", 4, []string{"unbreakableword", "x"}},
		{"", 10, []string{""}},
	} {
		if got := wrap(measure, c.in, c.width); !slices.Equal(got, c.want) {
			t.Errorf("wrap(%q, %d) = %q, want %q", c.in, c.width, got, c.want)
		}
	}
}
