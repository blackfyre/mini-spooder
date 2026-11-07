package main

import (
	"github.com/veandco/go-sdl2/sdl"

	"github.com/blackfyre/mini-spooder/handheld/internal/padmap"
)

// buttonTable is the SDL controller table: SDL's game-controller buttons to
// the TrimUI's controls. SDL names the face buttons by position (Xbox
// layout); the TrimUI's labels are Nintendo's, so SDL's A (bottom) is the
// TrimUI's B and SDL's X (left) its Y, as gabagool's default mapping and
// Spooder.pak have them. The mapping maps the controls by their labels; the
// on-device check (task 9.3) confirms it.
var buttonTable = []struct {
	sdl  sdl.GameControllerButton
	hand padmap.Buttons
}{
	{sdl.CONTROLLER_BUTTON_B, padmap.A},
	{sdl.CONTROLLER_BUTTON_A, padmap.B},
	{sdl.CONTROLLER_BUTTON_Y, padmap.X},
	{sdl.CONTROLLER_BUTTON_X, padmap.Y},
	{sdl.CONTROLLER_BUTTON_LEFTSHOULDER, padmap.L1},
	{sdl.CONTROLLER_BUTTON_RIGHTSHOULDER, padmap.R1},
	{sdl.CONTROLLER_BUTTON_DPAD_UP, padmap.Up},
	{sdl.CONTROLLER_BUTTON_DPAD_DOWN, padmap.Down},
	{sdl.CONTROLLER_BUTTON_DPAD_LEFT, padmap.Left},
	{sdl.CONTROLLER_BUTTON_DPAD_RIGHT, padmap.Right},
	{sdl.CONTROLLER_BUTTON_START, padmap.Start},
	{sdl.CONTROLLER_BUTTON_BACK, padmap.Select},
}

// keyTable is a desktop keyboard stand-in for a controller: the arrows for
// the D-pad, A, B, X and Y for the face buttons, Q and E for L1 and R1, 1
// and 3 held for L2 and R2, Enter for Start. Escape is Menu.
var keyTable = []struct {
	key  sdl.Scancode
	hand padmap.Buttons
}{
	{sdl.SCANCODE_UP, padmap.Up},
	{sdl.SCANCODE_DOWN, padmap.Down},
	{sdl.SCANCODE_LEFT, padmap.Left},
	{sdl.SCANCODE_RIGHT, padmap.Right},
	{sdl.SCANCODE_A, padmap.A},
	{sdl.SCANCODE_B, padmap.B},
	{sdl.SCANCODE_X, padmap.X},
	{sdl.SCANCODE_Y, padmap.Y},
	{sdl.SCANCODE_Q, padmap.L1},
	{sdl.SCANCODE_E, padmap.R1},
	{sdl.SCANCODE_RETURN, padmap.Start},
}

// menuButton is the TrimUI's Menu button (gabagool's default mapping). It
// never reaches the mapping: a press starts a leave.
const menuButton = sdl.CONTROLLER_BUTTON_GUIDE

// pad is the first SDL game controller, reopened when one is added or
// removed, and the keyboard. Without either the controls read at rest.
type pad struct {
	gc       *sdl.GameController
	keyboard bool
}

func newPad(keyboard bool) *pad {
	p := &pad{keyboard: keyboard}
	p.open()
	return p
}

func (p *pad) open() {
	p.close()
	for i := 0; i < sdl.NumJoysticks(); i++ {
		if sdl.IsGameController(i) {
			if gc := sdl.GameControllerOpen(i); gc != nil {
				p.gc = gc
				return
			}
		}
	}
}

func (p *pad) close() {
	if p.gc != nil {
		p.gc.Close()
		p.gc = nil
	}
}

// event handles one SDL event and tells whether it asks to leave: Menu
// pressed, the window closed, or Escape on a desktop keyboard.
func (p *pad) event(ev sdl.Event) bool {
	switch e := ev.(type) {
	case *sdl.QuitEvent:
		return true
	case *sdl.ControllerDeviceEvent:
		if e.Type == sdl.CONTROLLERDEVICEADDED || e.Type == sdl.CONTROLLERDEVICEREMOVED {
			p.open()
		}
	case *sdl.ControllerButtonEvent:
		return e.Type == sdl.CONTROLLERBUTTONDOWN && sdl.GameControllerButton(e.Button) == menuButton
	case *sdl.KeyboardEvent:
		return e.Type == sdl.KEYDOWN && e.Repeat == 0 && e.Keysym.Sym == sdl.K_ESCAPE
	}
	return false
}

// controls reads the controller's and the keyboard's state now. SDL keeps
// it current as the events are pumped.
func (p *pad) controls() padmap.Controls {
	var c padmap.Controls
	if p.gc != nil && p.gc.Attached() {
		c = padmap.Controls{
			LeftX:    p.gc.Axis(sdl.CONTROLLER_AXIS_LEFTX),
			LeftY:    p.gc.Axis(sdl.CONTROLLER_AXIS_LEFTY),
			RightX:   p.gc.Axis(sdl.CONTROLLER_AXIS_RIGHTX),
			RightY:   p.gc.Axis(sdl.CONTROLLER_AXIS_RIGHTY),
			TriggerL: p.gc.Axis(sdl.CONTROLLER_AXIS_TRIGGERLEFT),
			TriggerR: p.gc.Axis(sdl.CONTROLLER_AXIS_TRIGGERRIGHT),
		}
		for _, b := range buttonTable {
			if p.gc.Button(b.sdl) != 0 {
				c.Buttons |= b.hand
			}
		}
	}
	if p.keyboard {
		keys := sdl.GetKeyboardState()
		for _, k := range keyTable {
			if int(k.key) < len(keys) && keys[k.key] != 0 {
				c.Buttons |= k.hand
			}
		}
		if keys[sdl.SCANCODE_1] != 0 {
			c.TriggerL = 32767
		}
		if keys[sdl.SCANCODE_3] != 0 {
			c.TriggerR = 32767
		}
	}
	return c
}
