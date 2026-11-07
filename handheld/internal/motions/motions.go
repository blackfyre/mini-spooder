// Package motions names the robot's motions by their stock names (openspec
// change motion-playback; baseline/README.md's motion table): ids 1–15, 99
// (Centre) and 100 (Zero position). The screen shows them uppercase.
package motions

import (
	"fmt"
	"strings"
)

// The motion ids the pak requests or names.
const (
	Stop     = 0 // the command frame's stop; telemetry's "none"
	Standby  = 1
	Forward  = 2
	Backward = 3
	LeftSh   = 4
	RightSh  = 5
	TurnL    = 6
	TurnR    = 7
	Lie      = 8
	SayHi    = 9
	Fighting = 10
	PushUp   = 11
	Sleep    = 12
	Dance1   = 13
	Dance2   = 14
	Dance3   = 15
	Centre   = 99
	Zero     = 100
)

// names is the stock motion table.
var names = map[uint8]string{
	Standby:  "Standby",
	Forward:  "Forward",
	Backward: "Backward",
	LeftSh:   "Left shift",
	RightSh:  "Right shift",
	TurnL:    "Turn left",
	TurnR:    "Turn right",
	Lie:      "Lie",
	SayHi:    "Say Hi",
	Fighting: "Fighting",
	PushUp:   "Push up",
	Sleep:    "Sleep",
	Dance1:   "Dance 1",
	Dance2:   "Dance 2",
	Dance3:   "Dance 3",
	Centre:   "Centre",
	Zero:     "Zero position",
}

// IDs is every motion id with a stock name, in order.
func IDs() []uint8 {
	var ids []uint8
	for id := range 256 {
		if _, ok := names[uint8(id)]; ok {
			ids = append(ids, uint8(id))
		}
	}
	return ids
}

// Known tells whether id has a stock name.
func Known(id uint8) bool {
	_, ok := names[id]
	return ok
}

// Name is id's stock name, or "Motion <n>" for an id it does not know.
func Name(id uint8) string {
	if n, ok := names[id]; ok {
		return n
	}
	return fmt.Sprintf("Motion %d", id)
}

// Label is id's name as the screen shows it, uppercase: `MOTION <n>` for an
// unknown id; none (0) is ifNone, such as IDLE for the motion playing or
// NONE for the pending one.
func Label(id uint8, ifNone string) string {
	if id == Stop {
		return ifNone
	}
	return strings.ToUpper(Name(id))
}
