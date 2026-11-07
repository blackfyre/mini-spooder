package protocol

import (
	"bytes"
	"math/rand/v2"
	"testing"
)

func TestCRC16CheckValue(t *testing.T) {
	// The catalogue check value of CRC-16/CCITT-FALSE.
	if got := CRC16([]byte("123456789")); got != 0x29B1 {
		t.Fatalf("CRC16(123456789) = %#04x, want 0x29b1", got)
	}
}

func sampleTelemetry() Telemetry {
	return Telemetry{Playing: 14, Pending: 2, Counter: 5, Result: ResultAccepted, CommandSeq: 40, AgeMs: 10,
		Angles: [8]uint8{70, 90, 90, 110, 110, 90, 90, AngleNone}, Accepted: 300, Malformed: 1, Failsafes: 2}
}

func TestDatagramRoundTrip(t *testing.T) {
	c := EncodeCommand(Command{Motion: 9, Counter: 200}, 255)
	if len(c) != CommandFrameSize {
		t.Fatalf("command frame %d bytes, want %d", len(c), CommandFrameSize)
	}
	f, ok := DecodeDatagram(c)
	if !ok || f.Type != TypeCommand || f.Seq != 255 || f.Command != (Command{9, 200}) {
		t.Fatalf("command decoded %+v %v", f, ok)
	}
	tl := EncodeTelemetry(sampleTelemetry(), 7)
	if len(tl) != TelemetryFrameSize {
		t.Fatalf("telemetry frame %d bytes, want %d", len(tl), TelemetryFrameSize)
	}
	f, ok = DecodeDatagram(tl)
	if !ok || f.Type != TypeTelemetry || f.Telemetry != sampleTelemetry() {
		t.Fatalf("telemetry decoded %+v %v", f, ok)
	}
}

func TestDatagramRejects(t *testing.T) {
	good := EncodeTelemetry(sampleTelemetry(), 7)
	flipCRC := bytes.Clone(good)
	flipCRC[len(flipCRC)-1] ^= 0x40
	two := append(bytes.Clone(good), good...)
	for name, b := range map[string][]byte{
		"flipped CRC byte": flipCRC,
		"truncated":        good[:len(good)-1],
		"one extra byte":   append(bytes.Clone(good), 0),
		"byte before":      append([]byte{0}, good...),
		"two frames":       two,
		"empty":            nil,
	} {
		if f, ok := DecodeDatagram(b); ok {
			t.Errorf("%s: decoded %+v", name, f)
		}
	}
}

func TestDecoderCorruptedThenValid(t *testing.T) {
	a := EncodeCommand(Command{2, 1}, 1)
	a[5] ^= 0x01 // a corrupted payload byte
	b := EncodeCommand(Command{0, 2}, 2)
	var d Decoder
	got := append(d.Write(append(a, b...)), d.End()...)
	if len(got) != 1 || got[0].Seq != 2 || d.Rejected != 1 {
		t.Fatalf("frames %+v, rejected %d; want the second frame and one rejected", got, d.Rejected)
	}
}

func TestDecoderGarbageAndFalseStart(t *testing.T) {
	a := EncodeCommand(Command{2, 1}, 1)
	b := EncodeTelemetry(sampleTelemetry(), 2)
	in := append(bytes.Clone(a), 0x00, 0x13, 0x37, StartMarker, 0x01, 0x42)
	in = append(in, StartMarker) // a false start right before the frame
	in = append(in, b...)
	var d Decoder
	got := append(d.Write(in), d.End()...)
	if len(got) != 2 || got[0].Seq != 1 || got[1].Seq != 2 {
		t.Fatalf("frames %+v, want both", got)
	}
}

func TestDecoderIncompleteAtEnd(t *testing.T) {
	// A false start whose bytes would hide a complete frame: rejected at the
	// end of the input, and the frame behind it found.
	b := EncodeCommand(Command{2, 1}, 1)
	in := append([]byte{StartMarker, Version, TypeTelemetry, 0, TelemetryPayloadSize}, b...)
	var d Decoder
	got := d.Write(in)
	got = append(got, d.End()...)
	if len(got) != 1 || got[0].Command != (Command{2, 1}) {
		t.Fatalf("frames %+v, want the command", got)
	}
	if d.Held() != 0 {
		t.Fatalf("held %d bytes after End", d.Held())
	}
}

// Random input: the decoder never holds more than one maximum-size frame,
// and the bytes of returned frames plus the discarded bytes add up to the
// input.
func TestDecoderAccounting(t *testing.T) {
	r := rand.New(rand.NewPCG(1, 2))
	for range 2000 {
		var in []byte
		for len(in) < 200 {
			switch r.IntN(4) {
			case 0:
				in = append(in, EncodeCommand(Command{uint8(r.IntN(256)), uint8(r.IntN(256))}, uint8(r.IntN(256)))...)
			case 1:
				in = append(in, EncodeTelemetry(sampleTelemetry(), uint8(r.IntN(256)))...)
			case 2:
				in = append(in, StartMarker)
			default:
				in = append(in, uint8(r.IntN(256)))
			}
		}
		var d Decoder
		var frames []Frame
		for _, v := range in {
			frames = append(frames, d.Write([]byte{v})...)
			if d.Held() > MaxFrameSize {
				t.Fatalf("held %d bytes", d.Held())
			}
		}
		frames = append(frames, d.End()...)
		n := d.Discarded
		for _, f := range frames {
			n += HeaderSize + payloadSize(f.Type) + CRCSize
		}
		if n != len(in) {
			t.Fatalf("frames and discarded bytes account for %d of %d bytes", n, len(in))
		}
	}
}
