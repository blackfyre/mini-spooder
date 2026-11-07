package protocol

import (
	"bufio"
	"bytes"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

// goldenPath is drive-link's golden frames file, read in place (design A7,
// D11). Until drive-link's codec has written it, the tests read the stand-in
// written from the drive-link specification (testdata/gen_stand_in.py).
const (
	goldenPath  = "../../../core/test/golden/drive_link_v1.txt"
	standInPath = "testdata/drive_link_v1_stand_in.txt"
)

func goldenFile(t *testing.T) string {
	t.Helper()
	if _, err := os.Stat(goldenPath); err == nil {
		return goldenPath
	}
	t.Logf("%s does not exist yet: checking against %s", filepath.Clean(goldenPath), standInPath)
	return standInPath
}

// goldenLine is one line of the golden file.
type goldenLine struct {
	name     string
	raw      []byte
	rejected bool
	fields   map[string]uint64
	order    []string
}

func parseGolden(b []byte) ([]goldenLine, error) {
	var out []goldenLine
	sc := bufio.NewScanner(bytes.NewReader(b))
	sc.Buffer(make([]byte, 64*1024), 1024*1024)
	for n := 1; sc.Scan(); n++ {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		f := strings.Fields(line)
		if len(f) < 3 {
			return nil, fmt.Errorf("line %d: too few fields", n)
		}
		raw, err := hex.DecodeString(f[1])
		if err != nil {
			return nil, fmt.Errorf("line %d (%s): %v", n, f[0], err)
		}
		g := goldenLine{name: f[0], raw: raw, fields: map[string]uint64{}}
		if len(f) == 3 && f[2] == "rejected" {
			g.rejected = true
			out = append(out, g)
			continue
		}
		for _, kv := range f[2:] {
			k, v, ok := strings.Cut(kv, "=")
			if !ok {
				return nil, fmt.Errorf("line %d (%s): field %q", n, f[0], kv)
			}
			switch {
			case k == "type" && v == "command":
				g.fields[k] = TypeCommand
			case k == "type" && v == "telemetry":
				g.fields[k] = TypeTelemetry
			default:
				x, err := strconv.ParseUint(v, 10, 16)
				if err != nil {
					return nil, fmt.Errorf("line %d (%s): field %q: %v", n, f[0], kv, err)
				}
				g.fields[k] = x
			}
			g.order = append(g.order, k)
		}
		out = append(out, g)
	}
	return out, sc.Err()
}

// fieldsOf is a frame's fields under the golden file's names.
func fieldsOf(f Frame) map[string]uint64 {
	m := map[string]uint64{"version": uint64(f.Version), "type": uint64(f.Type), "seq": uint64(f.Seq)}
	switch f.Type {
	case TypeCommand:
		m["motion"], m["counter"] = uint64(f.Command.Motion), uint64(f.Command.Counter)
	case TypeTelemetry:
		t := f.Telemetry
		m["playing"], m["pending"], m["counter"] = uint64(t.Playing), uint64(t.Pending), uint64(t.Counter)
		m["result"], m["commandSeq"], m["ageMs"] = uint64(t.Result), uint64(t.CommandSeq), uint64(t.AgeMs)
		for i, a := range t.Angles {
			m[fmt.Sprintf("angle%d", i)] = uint64(a)
		}
		m["accepted"], m["malformed"], m["outOfSequence"] = uint64(t.Accepted), uint64(t.Malformed), uint64(t.OutOfSequence)
		m["otherSender"], m["rateLimited"], m["failsafes"] = uint64(t.OtherSender), uint64(t.RateLimited), uint64(t.Failsafes)
	}
	return m
}

// frameOf builds the frame a golden line's fields describe.
func frameOf(g goldenLine) (Frame, error) {
	f := Frame{Version: uint8(g.fields["version"]), Type: uint8(g.fields["type"]), Seq: uint8(g.fields["seq"])}
	want := fieldsOf(Frame{Type: f.Type})
	for _, k := range g.order {
		if _, ok := want[k]; !ok {
			return f, fmt.Errorf("unknown field %q", k)
		}
	}
	for k := range want {
		if _, ok := g.fields[k]; !ok {
			return f, fmt.Errorf("missing field %q", k)
		}
	}
	v := func(k string) uint8 { return uint8(g.fields[k]) }
	w := func(k string) uint16 { return uint16(g.fields[k]) }
	switch f.Type {
	case TypeCommand:
		f.Command = Command{Motion: v("motion"), Counter: v("counter")}
	case TypeTelemetry:
		t := &f.Telemetry
		t.Playing, t.Pending, t.Counter, t.Result = v("playing"), v("pending"), v("counter"), Result(v("result"))
		t.CommandSeq, t.AgeMs = v("commandSeq"), v("ageMs")
		for i := range t.Angles {
			t.Angles[i] = v(fmt.Sprintf("angle%d", i))
		}
		t.Accepted, t.Malformed, t.OutOfSequence = w("accepted"), w("malformed"), w("outOfSequence")
		t.OtherSender, t.RateLimited, t.Failsafes = w("otherSender"), w("rateLimited"), w("failsafes")
	default:
		return f, fmt.Errorf("unknown type %d", f.Type)
	}
	return f, nil
}

// checkGolden checks the codec against the golden file's contents and
// returns one error per frame that differs, each naming the frame.
func checkGolden(b []byte) []error {
	lines, err := parseGolden(b)
	if err != nil {
		return []error{err}
	}
	var errs []error
	fail := func(g goldenLine, format string, args ...any) {
		errs = append(errs, fmt.Errorf("%s: %s", g.name, fmt.Sprintf(format, args...)))
	}
	for _, g := range lines {
		got, ok := DecodeDatagram(g.raw)
		var d Decoder
		frames := append(d.Write(g.raw), d.End()...)
		if g.rejected {
			if ok || len(frames) != 0 {
				fail(g, "decoded a frame marked rejected")
			}
			if d.Rejected == 0 {
				fail(g, "not counted as rejected")
			}
			continue
		}
		want, err := frameOf(g)
		if err != nil {
			fail(g, "%v", err)
			continue
		}
		var enc []byte
		switch want.Type {
		case TypeCommand:
			enc = EncodeCommand(want.Command, want.Seq)
		case TypeTelemetry:
			enc = EncodeTelemetry(want.Telemetry, want.Seq)
		}
		if want.Version != Version {
			fail(g, "version %d, the codec's is %d", want.Version, Version)
		}
		if !bytes.Equal(enc, g.raw) {
			fail(g, "encodes to %x, golden %x", enc, g.raw)
		}
		if !ok {
			fail(g, "golden bytes do not decode")
			continue
		}
		if got != want {
			fail(g, "decodes to %+v, golden %+v", got, want)
		}
	}
	return errs
}

func TestGoldenFrames(t *testing.T) {
	b, err := os.ReadFile(goldenFile(t))
	if err != nil {
		t.Fatal(err)
	}
	lines, err := parseGolden(b)
	if err != nil {
		t.Fatal(err)
	}
	if len(lines) == 0 {
		t.Fatal("no golden frames")
	}
	for _, e := range checkGolden(b) {
		t.Error(e)
	}
}

// One byte of a golden frame changed: the check fails and names the frame.
func TestGoldenChangedByteNamesFrame(t *testing.T) {
	b, err := os.ReadFile(goldenFile(t))
	if err != nil {
		t.Fatal(err)
	}
	lines, err := parseGolden(b)
	if err != nil {
		t.Fatal(err)
	}
	for _, g := range lines {
		if g.rejected {
			continue
		}
		h := hex.EncodeToString(g.raw)
		// The sequence number's low hex digit, in a copy of the file.
		i := 7
		alt := byte('0')
		if h[i] == '0' {
			alt = '1'
		}
		changed := h[:i] + string(alt) + h[i+1:]
		edited := bytes.Replace(b, []byte(g.name+" "+h), []byte(g.name+" "+changed), 1)
		errs := checkGolden(edited)
		if len(errs) == 0 {
			t.Errorf("%s: changing one byte left the check passing", g.name)
			continue
		}
		for _, e := range errs {
			if !strings.HasPrefix(e.Error(), g.name+":") {
				t.Errorf("%s changed, but the check names another frame: %v", g.name, e)
			}
		}
	}
}

// The golden file covers what drive-link's Golden frames requirement asks.
func TestGoldenCoverage(t *testing.T) {
	b, err := os.ReadFile(goldenFile(t))
	if err != nil {
		t.Fatal(err)
	}
	lines, err := parseGolden(b)
	if err != nil {
		t.Fatal(err)
	}
	types := map[uint64]bool{}
	results := map[uint64]bool{}
	seq255, rejected := false, 0
	for _, g := range lines {
		if g.rejected {
			rejected++
			continue
		}
		types[g.fields["type"]] = true
		if g.fields["type"] == TypeTelemetry {
			results[g.fields["result"]] = true
		}
		seq255 = seq255 || g.fields["seq"] == 255
	}
	if !types[TypeCommand] || !types[TypeTelemetry] {
		t.Errorf("types covered %v, want command and telemetry", types)
	}
	for r := range uint64(4) {
		if !results[r] {
			t.Errorf("result %d not covered", r)
		}
	}
	if !seq255 || rejected < 4 {
		t.Errorf("sequence 255 covered %v, rejected frames %d", seq255, rejected)
	}
}
