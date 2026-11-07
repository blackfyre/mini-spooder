// Package fonts embeds the Blackfyre typefaces MiniSpooder.pak draws with,
// Archivo SemiBold (read text) and JetBrains Mono Regular (labels, numbers
// and keycaps), with their SIL Open Font License 1.1 texts, which the binary
// prints with --licences.
package fonts

import _ "embed"

var (
	//go:embed Archivo-SemiBold.ttf
	ArchivoSemiBold []byte
	//go:embed JetBrainsMono-Regular.ttf
	JetBrainsMonoRegular []byte

	//go:embed Archivo-OFL.txt
	ArchivoLicence string
	//go:embed JetBrainsMono-OFL.txt
	JetBrainsMonoLicence string
)
