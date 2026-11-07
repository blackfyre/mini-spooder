// Package theme is the Blackfyre colours MiniSpooder.pak draws with
// (openspec change handheld-pak, design D8, requirement Screen style), from
// the committed snapshot tokens.json of the design system's
// project/tokens.json. theme_gen.go is generated; never edit its values by
// hand.
package theme

//go:generate go run ./gen

// Color is an opaque RGB colour.
type Color struct{ R, G, B uint8 }
