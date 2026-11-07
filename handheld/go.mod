module github.com/blackfyre/mini-spooder/handheld

go 1.26.0

toolchain go1.27.1

require (
	github.com/BrandonKowalski/gabagool/v2 v2.26.6
	github.com/veandco/go-sdl2 v0.4.40
	go.uber.org/atomic v1.11.0
)

require (
	github.com/BrandonKowalski/certifiable v1.3.0 // indirect
	github.com/BurntSushi/toml v1.6.0 // indirect
	github.com/holoplot/go-evdev v0.0.0-20250804134636-ab1d56a1fe83 // indirect
	github.com/srwiley/oksvg v0.0.0-20221011165216-be6e8873101c // indirect
	github.com/srwiley/rasterx v0.0.0-20220730225603-2ab79fcdd4ef // indirect
	golang.org/x/exp/typeparams v0.0.0-20231108232855-2478ac86f678 // indirect
	golang.org/x/image v0.34.0 // indirect
	golang.org/x/mod v0.41.0 // indirect
	golang.org/x/net v0.59.0 // indirect
	golang.org/x/sync v0.23.0 // indirect
	golang.org/x/text v0.42.0 // indirect
	golang.org/x/tools v0.51.0 // indirect
	honnef.co/go/tools v0.8.2 // indirect
)

// gabagool v2.26.6 with the power-button hook
// (third_party/gabagool-power-hook.patch), made by `mise run handheld-fork`
// (design D5). Drop it once upstream has the hook.
replace github.com/BrandonKowalski/gabagool/v2 => ../build/handheld/gabagool

tool honnef.co/go/tools/cmd/staticcheck
