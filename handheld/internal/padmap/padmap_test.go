package padmap

import (
	"slices"
	"testing"
)

const full = 32767

func press(b Buttons) Controls { return Controls{Buttons: b} }

func TestOnePerPress(t *testing.T) {
	ev := Events(Controls{}, press(A))
	if len(ev) != 1 || ev[0].Motion != 9 || ev[0].Stop {
		t.Fatalf("A press: %+v, want motion 9", ev)
	}
	if ev := Events(press(A), press(A)); ev != nil {
		t.Errorf("A held: %+v, want nothing", ev)
	}
	if ev := Events(press(A), Controls{}); ev != nil {
		t.Errorf("A released: %+v, want nothing", ev)
	}
}

func TestLayerFromTrigger(t *testing.T) {
	r2 := Controls{TriggerR: full}
	ev := Events(r2, Controls{Buttons: A, TriggerR: full})
	if len(ev) != 1 || ev[0].Motion != 13 {
		t.Fatalf("R2 then A: %+v, want motion 13", ev)
	}
	// Exactly half travel is not held.
	half := Controls{TriggerR: TriggerHeld}
	if ev := Events(half, Controls{Buttons: A, TriggerR: TriggerHeld}); len(ev) != 1 || ev[0].Motion != 9 {
		t.Errorf("R2 at half travel then A: %+v, want motion 9", ev)
	}
}

func TestTriggerReleasedBeforeButton(t *testing.T) {
	l2 := Controls{TriggerL: full}
	l2y := Controls{Buttons: Y, TriggerL: full}
	var all []Event
	all = append(all, Events(l2, l2y)...)
	all = append(all, Events(l2y, press(Y))...) // L2 released, Y held
	all = append(all, Events(press(Y), Controls{})...)
	if len(all) != 1 || all[0].Motion != 100 {
		t.Fatalf("events %+v, want one request for motion 100", all)
	}
}

func TestBothTriggers(t *testing.T) {
	both := Controls{TriggerL: full, TriggerR: full}
	for _, b := range []Buttons{A, X, Y} {
		cur := both
		cur.Buttons = b
		if ev := Events(both, cur); ev != nil {
			t.Errorf("both triggers and %v: %+v, want nothing", b, ev)
		}
	}
	cur := both
	cur.Buttons = B
	if ev := Events(both, cur); len(ev) != 1 || !ev[0].Stop {
		t.Errorf("both triggers and B: %+v, want stop", ev)
	}
}

func TestStopInEveryLayer(t *testing.T) {
	for _, base := range []Controls{{}, {TriggerL: full}, {TriggerR: full}, {TriggerL: full, TriggerR: full}} {
		cur := base
		cur.Buttons = B
		ev := Events(base, cur)
		if len(ev) != 1 || !ev[0].Stop || ev[0].Motion != 0 {
			t.Errorf("layer %v: B gives %+v, want stop", LayerOf(base), ev)
		}
	}
}

func TestNothingForOtherControls(t *testing.T) {
	if ev := Events(Controls{}, press(Select)); ev != nil {
		t.Errorf("Select: %+v", ev)
	}
	sticks := Controls{LeftX: full, LeftY: -full, RightX: -full, RightY: full}
	if ev := Events(Controls{}, sticks); ev != nil {
		t.Errorf("sticks: %+v", ev)
	}
	if ev := Events(Controls{}, Controls{TriggerL: full, TriggerR: full}); ev != nil {
		t.Errorf("triggers alone: %+v", ev)
	}
}

// Every stock motion of the controller page is reachable, each exactly
// once, and no control requests motion 99.
func TestEveryStockMotionReachable(t *testing.T) {
	var got []uint8
	stops := 0
	for _, layer := range []Controls{{}, {TriggerR: full}, {TriggerL: full}} {
		for _, b := range []Buttons{Up, Down, Left, Right, L1, R1, Start, A, X, Y, B} {
			if layer != (Controls{}) && b != A && b != X && b != Y {
				continue
			}
			cur := layer
			cur.Buttons = b
			for _, e := range Events(layer, cur) {
				if e.Stop {
					stops++
				} else {
					got = append(got, e.Motion)
				}
			}
		}
	}
	slices.Sort(got)
	want := []uint8{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 100}
	if !slices.Equal(got, want) || stops != 1 {
		t.Fatalf("motions %v and %d stops, want %v and one stop", got, stops, want)
	}
	for _, e := range Table {
		if e.Motion == 99 {
			t.Errorf("%v requests motion 99", e.Control)
		}
	}
}

func TestWalkingControls(t *testing.T) {
	for _, e := range Table {
		want := e.Motion >= 2 && e.Motion <= 7 && !e.Stop
		if e.Walking != want {
			t.Errorf("%v (motion %d): walking %v, want %v", e.Control, e.Motion, e.Walking, want)
		}
	}
}

// Two presses in one sample: both events, the stop first.
func TestStopFirstInOneSample(t *testing.T) {
	ev := Events(Controls{}, press(Start|B))
	if len(ev) != 2 || !ev[0].Stop || ev[1].Motion != 1 {
		t.Fatalf("Start and B together: %+v, want stop then Standby", ev)
	}
}

func TestFaces(t *testing.T) {
	for _, c := range []struct {
		layer Layer
		want  [3]uint8
	}{
		{LayerBase, [3]uint8{9, 10, 11}},
		{LayerR2, [3]uint8{13, 14, 15}},
		{LayerL2, [3]uint8{8, 12, 100}},
		{LayerBoth, [3]uint8{0, 0, 0}},
	} {
		f := Faces(c.layer)
		for i := range f {
			if f[i].Motion != c.want[i] {
				t.Errorf("layer %v: faces %+v, want %v", c.layer, f, c.want)
				break
			}
		}
	}
}
