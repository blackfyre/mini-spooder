// Package padmap is the control mapping (openspec change handheld-pak,
// design D2, requirement Control mapping): the TrimUI's controls to motion
// and stop requests. It is a pure function of the previous and current pad
// state: an event only on a press edge, the layer read at the press, nothing
// on a hold or a release. Repetition of a held walking control is the app's.
// The table here is the one the screen labels its controls from.
package padmap

import "github.com/blackfyre/mini-spooder/handheld/internal/motions"

// Buttons is a set of the TrimUI's buttons, by their printed labels. Menu
// and Power are not among them: they never reach the mapping.
type Buttons uint16

const (
	Up Buttons = 1 << iota
	Down
	Left
	Right
	L1
	R1
	A
	B
	X
	Y
	Start
	Select
)

// Controls is the pad's state at one moment.
type Controls struct {
	Buttons Buttons
	// The analogue triggers, 0 (released) to 32767 (fully pressed), as SDL
	// reports them.
	TriggerL, TriggerR int16
	// The sticks request nothing; they are kept for completeness.
	LeftX, LeftY, RightX, RightY int16
}

// TriggerHeld is the travel beyond which a trigger counts as held: half.
const TriggerHeld = 32767 / 2

// Layer is the face buttons' layer.
type Layer int

const (
	LayerBase Layer = iota // no trigger held
	LayerR2                // R2 held: the dances
	LayerL2                // L2 held: Lie, Sleep, Zero position
	LayerBoth              // both held: A, X and Y request nothing
)

func (l Layer) String() string {
	switch l {
	case LayerR2:
		return "R2"
	case LayerL2:
		return "L2"
	case LayerBoth:
		return "L2 + R2"
	}
	return "BASE"
}

// LayerOf is the layer c holds.
func LayerOf(c Controls) Layer {
	l, r := c.TriggerL > TriggerHeld, c.TriggerR > TriggerHeld
	switch {
	case l && r:
		return LayerBoth
	case r:
		return LayerR2
	case l:
		return LayerL2
	}
	return LayerBase
}

// Entry is one row of the control table.
type Entry struct {
	Control Buttons
	// Layer applies to the face buttons A, X and Y only; the other controls
	// act in every layer (LayerAny).
	Layer   Layer
	Motion  uint8 // the motion requested; with Stop set, none
	Stop    bool
	Walking bool // a walking control (motions 2–7), repeated while held
}

// LayerAny marks a control that acts whatever trigger is held.
const LayerAny Layer = -1

// Table is the control mapping (requirement Control mapping). No control
// requests motion 99 (Centre): the stock controller page has no button for
// it. Select has no function.
var Table = []Entry{
	{Control: Up, Layer: LayerAny, Motion: motions.Forward, Walking: true},
	{Control: Down, Layer: LayerAny, Motion: motions.Backward, Walking: true},
	{Control: Left, Layer: LayerAny, Motion: motions.TurnL, Walking: true},
	{Control: Right, Layer: LayerAny, Motion: motions.TurnR, Walking: true},
	{Control: L1, Layer: LayerAny, Motion: motions.LeftSh, Walking: true},
	{Control: R1, Layer: LayerAny, Motion: motions.RightSh, Walking: true},
	{Control: Start, Layer: LayerAny, Motion: motions.Standby},
	{Control: B, Layer: LayerAny, Stop: true},
	{Control: A, Layer: LayerBase, Motion: motions.SayHi},
	{Control: X, Layer: LayerBase, Motion: motions.Fighting},
	{Control: Y, Layer: LayerBase, Motion: motions.PushUp},
	{Control: A, Layer: LayerR2, Motion: motions.Dance1},
	{Control: X, Layer: LayerR2, Motion: motions.Dance2},
	{Control: Y, Layer: LayerR2, Motion: motions.Dance3},
	{Control: A, Layer: LayerL2, Motion: motions.Lie},
	{Control: X, Layer: LayerL2, Motion: motions.Sleep},
	{Control: Y, Layer: LayerL2, Motion: motions.Zero},
}

// Event is one request the mapping asks for.
type Event struct {
	Control Buttons
	Stop    bool  // a stop request
	Motion  uint8 // otherwise the motion requested
	Walking bool
}

// order is the order of the controls within one sample, so that a stop
// pressed in the same sample as another control is taken first.
var order = []Buttons{B, Up, Down, Left, Right, L1, R1, Start, A, X, Y}

// lookup is the entry for control pressed in layer, if any.
func lookup(control Buttons, layer Layer) (Entry, bool) {
	for _, e := range Table {
		if e.Control == control && (e.Layer == LayerAny || e.Layer == layer) {
			return e, true
		}
	}
	return Entry{}, false
}

// Events is the requests for the presses from prev to cur: one per control
// pressed in cur and not in prev, the face buttons in cur's layer.
func Events(prev, cur Controls) []Event {
	pressed := cur.Buttons &^ prev.Buttons
	if pressed == 0 {
		return nil
	}
	layer := LayerOf(cur)
	var out []Event
	for _, c := range order {
		if pressed&c == 0 {
			continue
		}
		e, ok := lookup(c, layer)
		if !ok {
			continue
		}
		out = append(out, Event{Control: c, Stop: e.Stop, Motion: e.Motion, Walking: e.Walking})
	}
	return out
}

// Face is a face button's label in a layer: the key and the motion it
// requests (0 for none, in LayerBoth).
type Face struct {
	Key    string
	Motion uint8
}

// Faces is what A, X and Y request in layer.
func Faces(layer Layer) [3]Face {
	out := [3]Face{{Key: "A"}, {Key: "X"}, {Key: "Y"}}
	for i, c := range []Buttons{A, X, Y} {
		if e, ok := lookup(c, layer); ok && e.Layer != LayerAny {
			out[i].Motion = e.Motion
		}
	}
	return out
}
