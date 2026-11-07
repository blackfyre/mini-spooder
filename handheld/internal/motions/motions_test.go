package motions

import (
	"bufio"
	"os"
	"regexp"
	"strconv"
	"strings"
	"testing"
)

// readmeMotions reads baseline/README.md's motion table: rows "| n | Name |",
// "| 13–15 | Dance 1–3 |" for a range, and a name ending at a colon.
func readmeMotions(t *testing.T) map[uint8]string {
	t.Helper()
	f, err := os.Open("../../../baseline/README.md")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	single := regexp.MustCompile(`^\| (\d+) \| ([^|:]+?)(:[^|]*)? \|$`)
	span := regexp.MustCompile(`^\| (\d+)–(\d+) \| (.+) (\d+)–(\d+) \|$`)
	in := false
	out := map[uint8]string{}
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if strings.HasPrefix(line, "### Motions") {
			in = true
			continue
		}
		if !in {
			continue
		}
		if strings.HasPrefix(line, "#") {
			break
		}
		if m := single.FindStringSubmatch(line); m != nil {
			n, _ := strconv.Atoi(m[1])
			out[uint8(n)] = m[2]
		} else if m := span.FindStringSubmatch(line); m != nil {
			a, _ := strconv.Atoi(m[1])
			b, _ := strconv.Atoi(m[2])
			first, _ := strconv.Atoi(m[4])
			for i := a; i <= b; i++ {
				out[uint8(i)] = m[3] + " " + strconv.Itoa(first+i-a)
			}
		}
	}
	if len(out) == 0 {
		t.Fatal("no motion table found in baseline/README.md")
	}
	return out
}

func TestTableMatchesBaseline(t *testing.T) {
	want := readmeMotions(t)
	if len(want) != len(names) {
		t.Errorf("baseline has %d motions, the table %d", len(want), len(names))
	}
	for id, n := range want {
		if names[id] != n {
			t.Errorf("motion %d: table %q, baseline %q", id, names[id], n)
		}
	}
	ids := IDs()
	var expect []uint8
	for i := uint8(1); i <= 15; i++ {
		expect = append(expect, i)
	}
	expect = append(expect, Centre, Zero)
	if len(ids) != len(expect) {
		t.Fatalf("ids %v, want %v", ids, expect)
	}
	for i := range ids {
		if ids[i] != expect[i] {
			t.Fatalf("ids %v, want %v", ids, expect)
		}
	}
}

func TestLabels(t *testing.T) {
	for _, c := range []struct {
		id     uint8
		ifNone string
		want   string
	}{
		{14, "IDLE", "DANCE 2"},
		{2, "NONE", "FORWARD"},
		{99, "IDLE", "CENTRE"},
		{0, "IDLE", "IDLE"},
		{0, "NONE", "NONE"},
		{50, "IDLE", "MOTION 50"},
	} {
		if got := Label(c.id, c.ifNone); got != c.want {
			t.Errorf("Label(%d) = %q, want %q", c.id, got, c.want)
		}
	}
}
