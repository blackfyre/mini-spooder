// Package protocol is the pak's Go copy of the drive link's frames (openspec
// change drive-link, requirement Frame format; handheld-pak design D3): the
// framing with its CRC, the command and telemetry frames, and an incremental
// decoder that rejects and resynchronises. It is written from the drive-link
// specification and checked against its golden frames file (golden_test.go).
package protocol

import (
	"encoding/binary"
	"fmt"
)

// The framing (drive-link: Frame format).
const (
	StartMarker = 0xA5
	Version     = 1

	TypeCommand   = 1
	TypeTelemetry = 2

	HeaderSize = 5 // start marker, version, type, sequence, payload length
	CRCSize    = 2

	CommandPayloadSize   = 2
	TelemetryPayloadSize = 26

	CommandFrameSize   = HeaderSize + CommandPayloadSize + CRCSize   // 9
	TelemetryFrameSize = HeaderSize + TelemetryPayloadSize + CRCSize // 33
	// MaxFrameSize is the largest frame of either type.
	MaxFrameSize = TelemetryFrameSize
)

// Command is the remote's complete desired state (drive-link: Command
// frames).
type Command struct {
	// Motion is the motion of the remote's most recent request: 0 = stop,
	// otherwise a motion id.
	Motion uint8
	// Counter is the motion-request counter, advanced on every motion or
	// stop request.
	Counter uint8
}

// Result is the result of the last request the robot processed in the
// session (telemetry payload offset 3).
type Result uint8

const (
	ResultNone     Result = 0 // none yet in this session (the baseline)
	ResultAccepted Result = 1 // motion accepted by motion playback
	ResultStop     Result = 2 // stop requested
	ResultRejected Result = 3 // motion rejected by motion playback
)

func (r Result) String() string {
	switch r {
	case ResultNone:
		return "none"
	case ResultAccepted:
		return "accepted"
	case ResultStop:
		return "stop"
	case ResultRejected:
		return "rejected"
	}
	return fmt.Sprintf("result%d", uint8(r))
}

// AngleNone is a joint angle reported for a servo released or without a
// commanded angle; AgeUnknown an age of 255 ms or more.
const (
	AngleNone  = 255
	AgeUnknown = 255
)

// Telemetry is the robot's telemetry payload (drive-link: Telemetry frames).
type Telemetry struct {
	Playing uint8  // motion playing, 0 = none
	Pending uint8  // motion pending behind it, 0 = none
	Counter uint8  // counter value of the last request processed, or the baseline
	Result  Result // that request's result
	// CommandSeq is the sequence number of the newest accepted command
	// frame, AgeMs its age when this frame was built (AgeUnknown: 255 ms or
	// more).
	CommandSeq uint8
	AgeMs      uint8
	// Angles by servo index, whole degrees; AngleNone when released or not
	// commanded.
	Angles [8]uint8
	// The robot's link counts since boot, wrapping.
	Accepted      uint16
	Malformed     uint16
	OutOfSequence uint16
	OtherSender   uint16
	RateLimited   uint16
	Failsafes     uint16
}

// Frame is one decoded frame: its header and, by Type, its payload.
type Frame struct {
	Version   uint8
	Type      uint8
	Seq       uint8
	Command   Command
	Telemetry Telemetry
}

// CRC16 is CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no
// reflection, no final XOR.
func CRC16(b []byte) uint16 {
	crc := uint16(0xFFFF)
	for _, v := range b {
		crc ^= uint16(v) << 8
		for range 8 {
			if crc&0x8000 != 0 {
				crc = crc<<1 ^ 0x1021
			} else {
				crc <<= 1
			}
		}
	}
	return crc
}

// frame wraps payload in a header and CRC.
func frame(typ, seq uint8, payload []byte) []byte {
	b := make([]byte, 0, HeaderSize+len(payload)+CRCSize)
	b = append(b, StartMarker, Version, typ, seq, uint8(len(payload)))
	b = append(b, payload...)
	return binary.LittleEndian.AppendUint16(b, CRC16(b[1:]))
}

// EncodeCommand is the command frame carrying c with sequence number seq.
func EncodeCommand(c Command, seq uint8) []byte {
	return frame(TypeCommand, seq, []byte{c.Motion, c.Counter})
}

// EncodeTelemetry is the telemetry frame carrying t with sequence number
// seq (the fake robot's and the tests').
func EncodeTelemetry(t Telemetry, seq uint8) []byte {
	p := make([]byte, 0, TelemetryPayloadSize)
	p = append(p, t.Playing, t.Pending, t.Counter, uint8(t.Result), t.CommandSeq, t.AgeMs)
	p = append(p, t.Angles[:]...)
	for _, v := range []uint16{t.Accepted, t.Malformed, t.OutOfSequence, t.OtherSender, t.RateLimited, t.Failsafes} {
		p = binary.LittleEndian.AppendUint16(p, v)
	}
	return frame(TypeTelemetry, seq, p)
}

// payloadSize is the fixed payload length of typ, or -1 for an unknown type.
func payloadSize(typ uint8) int {
	switch typ {
	case TypeCommand:
		return CommandPayloadSize
	case TypeTelemetry:
		return TelemetryPayloadSize
	}
	return -1
}

// parse reads a complete, checked frame.
func parse(b []byte) Frame {
	f := Frame{Version: b[1], Type: b[2], Seq: b[3]}
	p := b[HeaderSize : len(b)-CRCSize]
	switch f.Type {
	case TypeCommand:
		f.Command = Command{Motion: p[0], Counter: p[1]}
	case TypeTelemetry:
		t := &f.Telemetry
		t.Playing, t.Pending, t.Counter, t.Result, t.CommandSeq, t.AgeMs = p[0], p[1], p[2], Result(p[3]), p[4], p[5]
		copy(t.Angles[:], p[6:14])
		for i, dst := range []*uint16{&t.Accepted, &t.Malformed, &t.OutOfSequence, &t.OtherSender, &t.RateLimited, &t.Failsafes} {
			*dst = binary.LittleEndian.Uint16(p[14+2*i:])
		}
	}
	return f
}

// Decoder is the incremental frame decoder. It holds at most one
// maximum-size frame of undecided bytes. A candidate frame (from a start
// marker) with an unknown version or type, a wrong payload length or a bad
// CRC is rejected and counted, and decoding resumes at the next start
// marker after the candidate's first byte; bytes before a start marker are
// discarded.
type Decoder struct {
	buf []byte
	// Rejected counts rejected candidate frames, Discarded every byte not
	// returned inside a frame.
	Rejected  int
	Discarded int
}

// Write feeds b and returns the frames completed, in order.
func (d *Decoder) Write(b []byte) []Frame {
	var out []Frame
	for _, v := range b {
		d.buf = append(d.buf, v)
		out = d.drain(out)
	}
	return out
}

// End tells the decoder that its input has ended (the end of a datagram):
// an incomplete candidate is rejected, and what follows its first byte is
// decoded again. It returns the frames found that way.
func (d *Decoder) End() []Frame {
	out := d.drain(nil)
	for len(d.buf) > 0 {
		d.reject()
		out = d.drain(out)
	}
	return out
}

// Held is the undecided bytes the decoder holds.
func (d *Decoder) Held() int { return len(d.buf) }

// reject drops the current candidate's first byte and skips to the next
// start marker.
func (d *Decoder) reject() {
	d.Rejected++
	d.Discarded++
	d.buf = d.buf[1:]
	d.skip()
}

// skip discards the bytes before the first start marker.
func (d *Decoder) skip() {
	i := 0
	for i < len(d.buf) && d.buf[i] != StartMarker {
		i++
	}
	d.Discarded += i
	d.buf = append(d.buf[:0], d.buf[i:]...)
}

// drain decides what it can of the held bytes: frames are appended to out,
// and an incomplete candidate stays held.
func (d *Decoder) drain(out []Frame) []Frame {
	for {
		d.skip()
		if len(d.buf) < HeaderSize {
			return out
		}
		n := payloadSize(d.buf[2])
		if d.buf[1] != Version || n < 0 || int(d.buf[4]) != n {
			d.reject()
			continue
		}
		size := HeaderSize + n + CRCSize
		if len(d.buf) < size {
			return out
		}
		cand := d.buf[:size]
		if binary.LittleEndian.Uint16(cand[size-CRCSize:]) != CRC16(cand[1:size-CRCSize]) {
			d.reject()
			continue
		}
		out = append(out, parse(cand))
		d.buf = append(d.buf[:0], d.buf[size:]...)
	}
}

// DecodeDatagram decodes one datagram: it succeeds only when the datagram
// is exactly one valid frame, with no byte before or after it.
func DecodeDatagram(b []byte) (Frame, bool) {
	if len(b) > MaxFrameSize {
		return Frame{}, false
	}
	var d Decoder
	frames := d.Write(b)
	frames = append(frames, d.End()...)
	if len(frames) != 1 || d.Discarded != 0 {
		return Frame{}, false
	}
	return frames[0], true
}
