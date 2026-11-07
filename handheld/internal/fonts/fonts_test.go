package fonts

import (
	"strings"
	"testing"
)

func TestEmbedded(t *testing.T) {
	for name, b := range map[string][]byte{"Archivo": ArchivoSemiBold, "JetBrains Mono": JetBrainsMonoRegular} {
		// A TrueType file starts with the sfnt version 0x00010000.
		if len(b) < 4 || string(b[:4]) != "\x00\x01\x00\x00" {
			t.Errorf("%s is not a TrueType font", name)
		}
	}
	for name, l := range map[string]string{"Archivo": ArchivoLicence, "JetBrains Mono": JetBrainsMonoLicence} {
		if !strings.Contains(l, "SIL OPEN FONT LICENSE") {
			t.Errorf("%s's licence is not the OFL", name)
		}
	}
}
