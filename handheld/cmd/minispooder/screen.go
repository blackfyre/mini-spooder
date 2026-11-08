package main

import (
	"fmt"
	"strings"

	"github.com/veandco/go-sdl2/gfx"
	"github.com/veandco/go-sdl2/sdl"
	"github.com/veandco/go-sdl2/ttf"

	"github.com/blackfyre/mini-spooder/handheld/internal/app"
	"github.com/blackfyre/mini-spooder/handheld/internal/fonts"
	"github.com/blackfyre/mini-spooder/handheld/internal/theme"
)

// The drive screen (design D8) in the Blackfyre dark app style: the window
// on bf-ink, panels on bf-ink-raised outlined with bf-shadow-outline-dark,
// wells on bf-ink-code, text in bf-surface, labels in uppercase JetBrains
// Mono in bf-ink-subtle, orange text in bf-accent-on-dark, status fills
// bf-ok and bf-warn with bf-ink text, always with a word. The layout is for
// 1280×720 and scales with the window's width: a header, the robot, controls
// and link panels, and the footer keycaps (the leave banner in their place
// while leaving).
const (
	layoutWidth = 1280
	margin      = 28
	panelGap    = 20
	panelPad    = 20
	panelRadius = 16
	wellRadius  = 10
	chipHeight  = 38
	keyHeight   = 40
	keyRadius   = 7
	keyEdge     = 3
	headerH     = 56
	labelGap    = 6
	dotRadius   = 6

	heroSize  = 56 // the motion playing
	titleSize = 40 // "MINISPOODER."
	valueSize = 28 // values in Archivo
	monoSize  = 26 // numbers
	microSize = 17 // labels, chips and keycaps
)

func col(c theme.Color) sdl.Color { return sdl.Color{R: c.R, G: c.G, B: c.B, A: 0xff} }

var (
	colWindow    = col(theme.Ink)
	colPanel     = col(theme.InkRaised)
	colWell      = col(theme.InkCode)
	colChip      = col(theme.InkChip)
	colKey       = col(theme.InkCardHover)
	colKeyEdge   = col(theme.InkDeep)
	colOutline   = col(theme.ShadowOutlineDark)
	colText      = col(theme.Surface)
	colMuted     = col(theme.InkSubtle)
	colAccent    = col(theme.Accent)
	colAccentTxt = col(theme.AccentOnDark)
	colOnAccent  = col(theme.OnAccent)
	colOk        = col(theme.Ok)
	colWarn      = col(theme.Warn)
	colOnStatus  = col(theme.Ink)
)

// toneColour is a status tone's fill.
func toneColour(t app.Tone) sdl.Color {
	switch t {
	case app.ToneOK:
		return colOk
	case app.ToneWarn:
		return colWarn
	case app.ToneAccent:
		return colAccent
	}
	return colMuted
}

type fontSet struct {
	hero, title, value *ttf.Font // Archivo
	mono, micro        *ttf.Font // JetBrains Mono
}

func openFont(data []byte, size int) (*ttf.Font, error) {
	rw, err := sdl.RWFromMem(data)
	if err != nil {
		return nil, fmt.Errorf("font: %w", err)
	}
	f, err := ttf.OpenFontRW(rw, 1, size)
	if err != nil {
		return nil, fmt.Errorf("font: %w", err)
	}
	return f, nil
}

func loadFonts(scale float64) (*fontSet, error) {
	px := func(v int) int { return int(float64(v)*scale + 0.5) }
	f := &fontSet{}
	for _, o := range []struct {
		dst  **ttf.Font
		data []byte
		size int
	}{
		{&f.hero, fonts.ArchivoSemiBold, heroSize},
		{&f.title, fonts.ArchivoSemiBold, titleSize},
		{&f.value, fonts.ArchivoSemiBold, valueSize},
		{&f.mono, fonts.JetBrainsMonoRegular, monoSize},
		{&f.micro, fonts.JetBrainsMonoRegular, microSize},
	} {
		ft, err := openFont(o.data, px(o.size))
		if err != nil {
			f.close()
			return nil, err
		}
		*o.dst = ft
	}
	return f, nil
}

func (f *fontSet) close() {
	for _, ft := range []*ttf.Font{f.hero, f.title, f.value, f.mono, f.micro} {
		if ft != nil {
			ft.Close()
		}
	}
}

// window is what the screen needs of gabagool's window, whose type lives in
// gabagool's internal package.
type window interface {
	GetWidth() int32
	GetHeight() int32
	Present()
}

type texKey struct {
	f *ttf.Font
	s string
	c sdl.Color
}

type texEntry struct {
	tex  *sdl.Texture
	w, h int32
	used bool
}

// screen draws the join and drive screens on gabagool's renderer.
type screen struct {
	win   window
	r     *sdl.Renderer
	f     *fontSet
	scale float64
	w, h  int32
	cache map[texKey]*texEntry
}

func newScreen(win window, r *sdl.Renderer) (*screen, error) {
	scale := float64(win.GetWidth()) / layoutWidth
	f, err := loadFonts(scale)
	if err != nil {
		return nil, err
	}
	return &screen{win: win, r: r, f: f, scale: scale, w: win.GetWidth(), h: win.GetHeight(), cache: map[texKey]*texEntry{}}, nil
}

func (s *screen) close() {
	for k, e := range s.cache {
		_ = e.tex.Destroy()
		delete(s.cache, k)
	}
	s.f.close()
}

func (s *screen) gap(v int) int32 { return int32(float64(v)*s.scale + 0.5) }

func (s *screen) texture(f *ttf.Font, str string, c sdl.Color) *texEntry {
	k := texKey{f, str, c}
	if e, ok := s.cache[k]; ok {
		e.used = true
		return e
	}
	surf, err := f.RenderUTF8Blended(str, c)
	if err != nil {
		return nil
	}
	defer surf.Free()
	tex, err := s.r.CreateTextureFromSurface(surf)
	if err != nil {
		return nil
	}
	e := &texEntry{tex: tex, w: surf.W, h: surf.H, used: true}
	s.cache[k] = e
	return e
}

// endFrame drops the textures this frame did not use.
func (s *screen) endFrame() {
	for k, e := range s.cache {
		if !e.used {
			_ = e.tex.Destroy()
			delete(s.cache, k)
			continue
		}
		e.used = false
	}
}

func textWidth(f *ttf.Font, str string) int32 {
	w, _, err := f.SizeUTF8(str)
	if err != nil {
		return 0
	}
	return int32(w)
}

// fit shortens str with an ellipsis until it is at most width wide in f.
func fit(f *ttf.Font, str string, width int32) string {
	if textWidth(f, str) <= width {
		return str
	}
	r := []rune(str)
	for len(r) > 0 {
		r = r[:len(r)-1]
		cut := strings.TrimRight(string(r), " ,") + "…"
		if textWidth(f, cut) <= width {
			return cut
		}
	}
	return ""
}

// text draws str at x, y, shortened to width, and returns its width.
func (s *screen) text(f *ttf.Font, str string, x, y, width int32, c sdl.Color) int32 {
	str = fit(f, str, width)
	if str == "" {
		return 0
	}
	e := s.texture(f, str, c)
	if e == nil {
		return 0
	}
	_ = s.r.Copy(e.tex, nil, &sdl.Rect{X: x, Y: y, W: e.w, H: e.h})
	return e.w
}

func (s *screen) fill(x, y, w, h, rad int32, c sdl.Color) {
	if w <= 0 || h <= 0 {
		return
	}
	if rad <= 0 {
		_ = s.r.SetDrawColor(c.R, c.G, c.B, c.A)
		_ = s.r.FillRect(&sdl.Rect{X: x, Y: y, W: w, H: h})
		return
	}
	gfx.RoundedBoxColor(s.r, x, y, x+w-1, y+h-1, min(rad, h/2, w/2), c)
}

// panel draws a card with its kicker ("01 · ROBOT") and returns the y below
// the kicker.
func (s *screen) panel(x, y, w, h int32, number, title string, titleColour sdl.Color) int32 {
	s.fill(x-1, y-1, w+2, h+2, s.gap(panelRadius)+1, colOutline)
	s.fill(x, y, w, h, s.gap(panelRadius), colPanel)
	kx, ky := x+s.gap(panelPad), y+s.gap(panelPad)
	kx += s.text(s.f.micro, number, kx, ky, w, colAccentTxt)
	kx += s.text(s.f.micro, " · ", kx, ky, w, colMuted)
	s.text(s.f.micro, title, kx, ky, w, titleColour)
	return ky + int32(s.f.micro.Height()) + s.gap(14)
}

// chip draws a pill with an optional dot and returns its width.
func (s *screen) chip(x, y int32, label string, face, fg sdl.Color, dot *sdl.Color) int32 {
	w := s.chipWidth(label, dot != nil)
	h := s.gap(chipHeight)
	s.fill(x, y, w, h, h/2, face)
	tx := x + s.gap(14)
	if dot != nil {
		r := s.gap(dotRadius)
		gfx.FilledCircleColor(s.r, tx+r, y+h/2, r, *dot)
		gfx.AACircleColor(s.r, tx+r, y+h/2, r, *dot)
		tx += 2*r + s.gap(10)
	}
	s.text(s.f.micro, label, tx, y+(h-int32(s.f.micro.Height()))/2, w, fg)
	return w
}

func (s *screen) chipWidth(label string, dot bool) int32 {
	w := 2*s.gap(14) + textWidth(s.f.micro, label)
	if dot {
		w += 2*s.gap(dotRadius) + s.gap(10)
	}
	return w
}

// keycap draws a raised key and returns its width.
func (s *screen) keycap(x, y int32, label string) int32 {
	h := s.gap(keyHeight)
	w := max(h, 2*s.gap(12)+textWidth(s.f.micro, label))
	s.fill(x, y+s.gap(keyEdge), w, h, s.gap(keyRadius), colKeyEdge)
	s.fill(x, y, w, h, s.gap(keyRadius), colKey)
	tw := textWidth(s.f.micro, label)
	s.text(s.f.micro, label, x+(w-tw)/2, y+(h-int32(s.f.micro.Height()))/2, w, colText)
	return w
}

// cell draws a label over a value and returns the cell's height.
func (s *screen) cell(x, y, w int32, label, value string, mono bool, c sdl.Color) int32 {
	s.text(s.f.micro, label, x, y, w, colMuted)
	vy := y + int32(s.f.micro.Height()) + s.gap(labelGap)
	f := s.f.value
	if mono {
		f = s.f.mono
	}
	s.text(f, value, x, vy, w, c)
	return int32(s.f.micro.Height()) + s.gap(labelGap) + int32(s.f.value.Height())
}

func (s *screen) draw(sc app.Screen) {
	s.fill(0, 0, s.w, s.h, 0, colWindow)
	s.header(sc)
	bodyY := s.gap(margin) + s.gap(headerH) + s.gap(12)
	footerY := s.footerY()
	bodyH := footerY - s.gap(panelGap) - bodyY
	colW := (s.w - 2*s.gap(margin) - 2*s.gap(panelGap)) / 3
	x0 := s.gap(margin)
	robotW := colW + colW/3
	ctlW := colW
	linkW := s.w - 2*s.gap(margin) - 2*s.gap(panelGap) - robotW - ctlW
	s.robot(sc.Robot, x0, bodyY, robotW, bodyH)
	s.controls(sc.Controls, x0+robotW+s.gap(panelGap), bodyY, ctlW, bodyH)
	s.link(sc.Link, x0+robotW+ctlW+2*s.gap(panelGap), bodyY, linkW, bodyH)
	if sc.Leaving != "" {
		s.banner(sc.Leaving, footerY)
	} else {
		s.footer(sc.Footer, footerY)
	}
	s.endFrame()
	s.win.Present()
}

// title draws "MINISPOODER." with the orange full stop at the top left and
// returns the header's top.
func (s *screen) title() int32 {
	y := s.gap(margin) - s.gap(4)
	tx := s.gap(margin)
	ty := y + (s.gap(headerH)-int32(s.f.title.Height()))/2
	tx += s.text(s.f.title, app.Title, tx, ty, s.w/2, colText)
	s.text(s.f.title, ".", tx, ty, s.w/2, colAccent)
	return y
}

// footerY is the top of the footer keycaps (or the leave banner).
func (s *screen) footerY() int32 {
	return s.h - s.gap(margin) - s.gap(keyHeight) - s.gap(keyEdge)
}

// header: "MINISPOODER." with the orange full stop, then the link and
// signal chips.
func (s *screen) header(sc app.Screen) {
	y := s.title()

	cy := y + (s.gap(headerH)-s.gap(chipHeight))/2
	linkDot := toneColour(sc.LinkChipTone)
	sigDot := toneColour(sc.SignalTone)
	sig := sc.Signal
	if sc.SignalLevel != "UNKNOWN" {
		sig += " · " + sc.SignalLevel
	}
	x := s.w - s.gap(margin)
	for _, c := range []struct {
		label string
		dot   *sdl.Color
	}{{sig, &sigDot}, {sc.LinkChip, &linkDot}} {
		x -= s.chipWidth(c.label, true)
		s.chip(x, cy, c.label, colChip, colText, c.dot)
		x -= s.gap(10)
	}
	if sc.Notice != "" {
		s.text(s.f.micro, "NOTICE "+sc.Notice, s.gap(margin), y+s.gap(headerH), s.w-2*s.gap(margin), colWarn)
	}
}

// robot: the motion large, PLAYING or IDLE, the pending motion, the joint
// wells and the robot's link counts.
func (s *screen) robot(rb app.Robot, x, y, w, h int32) {
	title, tc := "ROBOT", colMuted
	if rb.Stale {
		title, tc = "ROBOT · STALE", colWarn
	}
	iy := s.panel(x, y, w, h, "01", title, tc)
	ix, iw := x+s.gap(panelPad), w-2*s.gap(panelPad)
	val := colText
	if rb.Stale {
		val = colMuted
	}
	s.text(s.f.hero, rb.Motion, ix, iy, iw, val)
	iy += int32(s.f.hero.Height()) + s.gap(4)
	face, fg := colChip, colText
	if rb.Activity == "PLAYING" && !rb.Stale {
		face, fg = colOk, colOnStatus
	}
	cw := s.chip(ix, iy, rb.Activity, face, fg, nil)
	s.text(s.f.micro, "PENDING", ix+cw+s.gap(16), iy+(s.gap(chipHeight)-int32(s.f.micro.Height()))/2, iw, colMuted)
	s.text(s.f.value, rb.Pending, ix+cw+s.gap(16)+textWidth(s.f.micro, "PENDING ")+s.gap(6),
		iy+(s.gap(chipHeight)-int32(s.f.value.Height()))/2, iw-cw-s.gap(120), val)
	iy += s.gap(chipHeight) + s.gap(16)

	// Eight joint wells, four across: index, GPIO and degrees.
	wellW := (iw - 3*s.gap(10)) / 4
	wellH := 2*s.gap(10) + int32(s.f.micro.Height()) + s.gap(4) + int32(s.f.mono.Height())
	for i, j := range rb.Joints {
		wx := ix + int32(i%4)*(wellW+s.gap(10))
		wy := iy + int32(i/4)*(wellH+s.gap(10))
		s.fill(wx, wy, wellW, wellH, s.gap(wellRadius), colWell)
		s.text(s.f.micro, fmt.Sprintf("S%d · GPIO %d", j.Index, j.GPIO), wx+s.gap(10), wy+s.gap(10), wellW-s.gap(20), colMuted)
		s.text(s.f.mono, j.Angle, wx+s.gap(10), wy+s.gap(10)+int32(s.f.micro.Height())+s.gap(4), wellW-s.gap(20), val)
	}
	iy += 2*wellH + s.gap(10) + s.gap(16)

	// The robot's link counts, three across.
	s.text(s.f.micro, "ROBOT LINK", ix, iy, iw, colMuted)
	iy += int32(s.f.micro.Height()) + s.gap(8)
	cellW := (iw - 2*s.gap(12)) / 3
	for i, st := range rb.Stats {
		cx := ix + int32(i%3)*(cellW+s.gap(12))
		cy := iy + int32(i/3)*(int32(s.f.micro.Height())+int32(s.f.mono.Height())+s.gap(10))
		s.text(s.f.micro, st.Label, cx, cy, cellW, colMuted)
		s.text(s.f.mono, st.Value, cx, cy+int32(s.f.micro.Height()), cellW, val)
	}
}

// controls: the layer, A, X and Y with this layer's motions, and the last
// request with its status.
func (s *screen) controls(cv app.ControlsView, x, y, w, h int32) {
	iy := s.panel(x, y, w, h, "02", "CONTROLS", colMuted)
	ix, iw := x+s.gap(panelPad), w-2*s.gap(panelPad)
	iy += s.cell(ix, iy, iw, "LAYER", cv.Layer, true, colAccentTxt) + s.gap(14)
	for _, f := range cv.Faces {
		kw := s.keycap(ix, iy, f.Key)
		s.text(s.f.value, f.Label, ix+kw+s.gap(14), iy+(s.gap(keyHeight)-int32(s.f.value.Height()))/2, iw-kw-s.gap(14), colText)
		iy += s.gap(keyHeight) + s.gap(keyEdge) + s.gap(10)
	}
	iy += s.gap(10)
	iy += s.cell(ix, iy, iw, "LAST REQUEST", cv.Last, false, colText) + s.gap(10)
	if cv.Status != "" {
		face := toneColour(cv.StatusTone)
		fg := colOnStatus
		if cv.StatusTone == app.ToneMuted {
			face, fg = colChip, colText
		}
		s.chip(ix, iy, cv.Status, face, fg, nil)
		iy += s.gap(chipHeight) + s.gap(10)
	}
	if cv.Repeating {
		s.chip(ix, iy, "REPEATING", colAccent, colOnAccent, nil)
	}
}

// link: the link's quality.
func (s *screen) link(lv app.LinkView, x, y, w, h int32) {
	iy := s.panel(x, y, w, h, "03", "LINK", colMuted)
	ix, iw := x+s.gap(panelPad), w-2*s.gap(panelPad)
	warn := func(n int) sdl.Color {
		if n > 0 {
			return colWarn
		}
		return colText
	}
	since := colText
	if !lv.HaveFrame || lv.SinceLast >= app.StaleAfter {
		since = colWarn
	}
	for _, c := range []struct {
		label, value string
		colour       sdl.Color
	}{
		{"FRAMES / S", fmt.Sprint(lv.PerSecond), colText},
		{"ROUND TRIP", lv.RTT, colText},
		{"SINCE LAST FRAME", lv.SinceLastS, since},
		{"INVALID", fmt.Sprint(lv.Invalid), warn(lv.Invalid)},
		{"LOST · LATE", fmt.Sprintf("%d · %d", lv.Lost, lv.Late), warn(lv.Lost + lv.Late)},
		{"SEND ERRORS", fmt.Sprint(lv.SendErrors), warn(lv.SendErrors)},
	} {
		iy += s.cell(ix, iy, iw, c.label, c.value, true, c.colour) + s.gap(12)
	}
}

// footer: the controls for stop, standby and leaving, as keycaps.
func (s *screen) footer(keys []app.FooterKey, y int32) {
	x := s.gap(margin)
	ty := y + (s.gap(keyHeight)-int32(s.f.micro.Height()))/2
	for i, k := range keys {
		if i > 0 {
			x += s.gap(28)
		}
		x += s.keycap(x, y, k.Key) + s.gap(10)
		x += s.text(s.f.micro, k.Does, x, ty, s.w, colMuted)
	}
}

// banner: an orange band with bf-on-accent text in the footer's place.
func (s *screen) banner(text string, y int32) {
	h := s.gap(keyHeight) + s.gap(keyEdge)
	s.fill(s.gap(margin), y, s.w-2*s.gap(margin), h, s.gap(wellRadius), colAccent)
	s.text(s.f.micro, text, s.gap(margin)+s.gap(20), y+(h-int32(s.f.micro.Height()))/2, s.w, colOnAccent)
}

// The join screen's panel.
const (
	joinPanelW = 860 // at most, centred
	barHeight  = 14
	lineGap    = 8 // between wrapped lines of read text
)

// drawJoin draws the join screen: the title, one panel with the status
// kicker, the message in Archivo and the join's progress, and the footer
// keycaps.
func (s *screen) drawJoin(v app.JoinView) {
	s.fill(0, 0, s.w, s.h, 0, colWindow)
	s.title()
	pw := min(s.gap(joinPanelW), s.w-2*s.gap(margin))
	tw := pw - 2*s.gap(panelPad)
	lines := wrap(func(str string) int32 { return textWidth(s.f.value, str) }, v.Message, tw)
	lh := int32(s.f.value.Height()) + s.gap(lineGap)
	ph := 2*s.gap(panelPad) + int32(s.f.micro.Height()) + s.gap(14) + int32(len(lines))*lh
	if v.ShowProgress {
		ph += s.gap(16) + max(s.gap(barHeight), int32(s.f.mono.Height()))
	}
	top := s.gap(margin) + s.gap(headerH)
	footerY := s.footerY()
	px := (s.w - pw) / 2
	py := top + max(0, (footerY-top-ph)/2)
	tc := colMuted
	if v.StatusTone != app.ToneMuted {
		tc = toneColour(v.StatusTone)
	}
	iy := s.panel(px, py, pw, ph, "01", v.Status, tc)
	ix := px + s.gap(panelPad)
	for _, l := range lines {
		s.text(s.f.value, l, ix, iy, tw, colText)
		iy += lh
	}
	if v.ShowProgress {
		iy += s.gap(16) - s.gap(lineGap)
		pct := fmt.Sprintf("%3d%%", int(v.Progress*100+0.5))
		pctW := textWidth(s.f.mono, "100%")
		bw := tw - pctW - s.gap(16)
		rowH := max(s.gap(barHeight), int32(s.f.mono.Height()))
		by := iy + (rowH-s.gap(barHeight))/2
		s.fill(ix, by, bw, s.gap(barHeight), s.gap(barHeight)/2, colWell)
		s.fill(ix, by, int32(float64(bw)*v.Progress), s.gap(barHeight), s.gap(barHeight)/2, colAccent)
		s.text(s.f.mono, pct, ix+tw-pctW, iy+(rowH-int32(s.f.mono.Height()))/2, pctW, colText)
	}
	s.footer(v.Footer, footerY)
	s.endFrame()
	s.win.Present()
}

// wrap breaks str into lines at most width wide as measure measures them,
// at spaces and at the line breaks in str. A word wider than width keeps a
// line of its own (text shortens it when drawn).
func wrap(measure func(string) int32, str string, width int32) []string {
	var lines []string
	for _, para := range strings.Split(str, "\n") {
		line := ""
		for _, word := range strings.Fields(para) {
			if line == "" {
				line = word
				continue
			}
			if next := line + " " + word; measure(next) <= width {
				line = next
				continue
			}
			lines = append(lines, line)
			line = word
		}
		lines = append(lines, line)
	}
	return lines
}
