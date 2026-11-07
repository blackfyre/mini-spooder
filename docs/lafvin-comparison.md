# LAFVIN vendor source versus dumped stock firmware

## Verdict

**Inferred, high confidence: modified firmware from the same motion-code lineage; the downloaded LAFVIN sketch is not the exact source of the stock image.** Shared programme numbers, eight complete angle sequences, the identical Lie table, the interpolation algorithm, signed-byte calibration and portal control conventions establish a close relationship. The portal, several gestures, timings, startup pin assignments, centre pose and SDK build differ. These observations do **not** prove that LAFVIN originally authored the stock firmware or establish which organisation changed which revision.

**Verified:** The current bundle contains one complete robot portal sketch, `references/lafvin/extracted/0.SpiderBot/0.SpiderBot.ino`, its motion and web-server files, and a precompiled `BIN/SpiderBot.ino.bin`. All 15 vendor motion tables occur byte-for-byte in that precompiled image; it is nevertheless different from stock. The six other top-level lesson sketches are servo, IR, walking or rangefinder demonstrations, not the complete motion portal (their includes/setup and file names are listed in the archive).

## Evidence conventions and acquisition

**Verified (V)** = directly observed source text, decoded bytes, byte comparison or disassembled instructions. **Inferred (I)** = interpretation of those facts, including assigning names to stripped binary functions and predicting runtime behaviour. **Unknown (U)** = not established. Each table row carries its own status; subsequent labelled paragraphs apply the label to the whole paragraph. Addresses written as **flash** are absolute offsets in the 4 MB dump. For mapped application code/data, `virtual address = 0x40200000 + flash offset`; RAM-initialisation segments use the separate mapping below.

**V:** On 7 November 2026 the supplied [Dropbox download](https://www.dropbox.com/scl/fo/a6ho5cf95ha65esq2odw6/AJXY-Am6qb03g0PRiQOCCNQ?rlkey=hmndkdpv0zjwaymovmp16hevn&st=n1o5zzxr&dl=1) succeeded. It is retained as `references/lafvin/bundle.zip`, 40,728,491 bytes. ZIP extraction rejected traversal/symlinks and skipped the single `/` directory placeholder; 1,924 other entries were extracted under `references/lafvin/extracted/`. Archive timestamps for the portal sources and binary say 15 April 2026; **I:** these are packaging metadata, not proof of authorship or compilation date. The [course index](https://lafvin-quadruped-spider-robot.readthedocs.io/en/latest/) returned HTTP 403 to the direct fetch; the web tool also could not retrieve it. No course fallback was needed.

| V: artefact | SHA-256 |
|---|---|
| `baseline/original-flash-4MB.bin` | `a477d40f405df5dc50b64d7fff90c7d89173d6c750f351aa12084e1bc1f143f7` |
| `references/lafvin/bundle.zip` | `1f593f103f44a8ff0ef306f4473e7710d499baf05aa5d996398acc44b51ef032` |
| `references/lafvin/extracted/0.SpiderBot/BIN/SpiderBot.ino.bin` | `2924db3bcbc718d3facde74dc5b4c6452dc2b9f1b7600cfc8991dac18f200ca4` |

**V:** The baseline hash was independently recomputed. Analysis used file reads, analyst-written Python and local objdump only; no serial port was opened and no hardware command was issued. No vendor script, project configuration or build file was executed. `baseline/`, `mise.toml` and `.gitignore` were not edited.

**V:** Supporting evidence is retained in `references/lafvin/`: `analyse.py` regenerates `motion-evidence.json` and [lafvin-motions.md](lafvin-motions.md); `stock-irom.bin` and `stock-iram.bin` are extracted application segments; `stock-irom.asm`/`stock-iram.asm` are linear disassemblies; `stock-control-blocks.asm` follows selected control-flow blocks; `stock-portal-fragments.txt` preserves readable portal fragments with absolute offsets. The full linear disassemblies include data and padding interpreted as instructions: only aligned, traced code blocks support behavioural findings here.

## Differences by area

**V:** In source citations below, `S` means `references/lafvin/extracted/0.SpiderBot/`; line numbers refer to the files exactly as downloaded. Full motion rows and per-row evidence are in [lafvin-motions.md](lafvin-motions.md).

| Area | Vendor source | Stock dump | Status and evidence |
|---|---|---|---|
| Identity | `QuadBot-E`, firmware `1.0.0`, protocol `v1`; debug banner `QuadBot-E Start!` | `Q1 mini Start!`; no `QuadBot` or `/api/v1` string in the application | V: S/webserver.cpp:12–16; S/0.SpiderBot.ino:60; flash `0x4b036`; literal searches over `0x1000–0x4dfff` |
| HTTP registration | `/`, `/debug/open`, `/debug/close`, `/controller`, `/save`, `/setting1`, six API paths | `/controller`, `/save`, `/`, `/editor`, `/zero`, `/setting`, `/online`, all registered with method value 1 | V: S/webserver.cpp:895–912; stock registration flash `0x1c60–0x1d8b`, literal pairs `0x1c14–0x1c54`. I: method 1 corresponds to Arduino HTTP_GET |
| Motion request | `/controller?pm=n` validates against the action registry, queues programme and returns `OK`; debug gate applies | `pm` string converted to integer and stored without action validation; returns an HTML echo; dispatch later ignores unsupported IDs | V: S/webserver.cpp:859–890; flash `0x3814–0x38f4`, dispatch `0x5084–0x51e3`. I: binary functions identified by their call paths |
| `pms` | No `pms` argument handler or JavaScript | JavaScript defines `controlPms`, but handler extracts only `pm`, `servo`, `value` | V: stock fragment `controller?pms=`; flash `0x3814–0x38f4`, literals `0x3800`, `0x3804`, `0x3630`. I: `pms` is unused UI residue, not a verified alternative motion command |
| Direct servo command | Validates index 0–7 and presence of value, queues it for `loop()` | Converts index/value and immediately calls the servo-writing routine from the HTTP handler | V: S/webserver.cpp:877–887; S/0.SpiderBot.ino:102–109; flash `0x387d–0x38af`. I: request timing differs even though both accept the same query shape |
| Controller page `/` | Dark single-page debug UI with fetch requests, hidden until `/debug/open`; root initially serves a locked notice | Old table of coloured buttons; XHR `pm` and unused `pms`; title/body assembled from String fragments | V: S/webserver.cpp:47–215,645–654; root handler flash `0x49ec–0x4cea`, page fragments `0x4cc41–0x4d2ae` |
| Motion editor `/editor` | Route/page removed | Eight sliders, text fields, generated comma-separated Action Code; defaults `70,90,90,110,110,90,90,70`; range 45–135 | V: vendor route list; stock handler `0x454c`, fragments `0x4be56–0x4cc42` |
| Zero page `/zero` | Route/page removed; single-page UI retains pm=100 button | Eight direct-servo buttons and pm=100, title `Zero check` | V: S/webserver.cpp:162; stock handler `0x3cac`, fragments `0x4b0bd–0x4b74b` |
| Calibration page | Overlay fetches JSON from `/setting1`; offset inputs recommend -30…30; API calibration also available | `/setting` builds HTML using eight stored offsets; reset-all button is commented out in HTML, but `/save?key=100` exists | V: S/webserver.cpp:133–185,658–670; stock handler `0x3fec`, calls to read helper at `0x4003–0x4088`; fragments `0x4b74c–0x4be55` |
| `/save` | Debug-gated signed-byte write, commits, returns plain `OK`; does not detach servos | Detaches all eight, writes/commits, reattaches on a different pin order from startup, returns HTML key/value echo | V: S/webserver.cpp:834–856,219–229; flash `0x3644–0x37fc`, write helper `0x1500–0x1527` |
| `/online` | Removed | Extracts `m0`…`m7` and `t1`, converts them to a local 9-integer row, calls runner with count 1, then returns an HTML echo of m0/m1 | V: flash `0x3920–0x3b03`, argument pointers `0x38f8–0x391c`, runner call `0x3a7b–0x3a7f`. I: this is an online single-keyframe player, not an online-status endpoint |
| Programme IDs | 1–15, 21 avoidance, 99 centre, 100 zero | 1–15, 99 centre, 100 zero | V: S/webserver.h:7–27; S/0.SpiderBot.ino:268–326; flash `0x5096–0x5118` |
| Motion data | 130 rows across 15 tables | 123 rows across 15 tables | V: source parsing plus dispatch/count/byte extraction in `motion-evidence.json`; detailed differences below |
| Servo pins | Always `14,12,13,15,16,5,4,2` | Startup `15,13,12,14,2,4,5,16`; after `/save`, `16,5,4,2,14,12,13,15` | V: S/spiderbot_motion.cpp:129–140; flash `0x1eb2–0x1f17`, `0x369e–0x36d3`, `0x373d–0x37a2`; dispatch-to-object map `0x1334–0x1364` |
| Zero/centre poses | Zero `135,45,135,45,45,135,45,135`; centre `60,90,90,120,120,90,90,60` | Both direct zero and centre arrays contain `135,45,135,45,45,135,45,135` | V: S/actionarray.h:21,25; flash `0x40de8`, `0x40e0c`; direct writers `0x1380`, `0x13d0`. I: pm=99 is effectively another zero command on stock, despite the source concept of centre |
| Calibration storage | EEPROM.begin(512), bytes 0–7 interpreted as signed offsets, one byte per index; no magic/version/CRC in application format | Same 512-byte buffer, signed-byte addition and per-key write/commit; sector fixed by linker address to `0x3fb000` | V: S/webserver.cpp:219–229,634; S/spiderbot_motion.cpp:39; flash `0x1fca–0x1fd0`, `0x130d–0x1326`, `0x1500–0x1527`, constructor `0x5494–0x54ad` |
| AP credentials | `Robot - ` plus uppercase unpadded hexadecimal final two AP-MAC bytes; password `12345678`; no explicit softAPConfig | Literal `Spider Robot`, password `12345678`; setup computes MAC text then replaces AP name with that literal; no explicit AP IP configuration in the traced setup | V: S/webserver.cpp:4,618–635; flash `0x1f1a–0x1fc0`; strings `0x4b045`, `0x4d2e1`; SDK saved SSID/password at `0x3fd0b4`/`0x3fd0d4` |
| AP IP | Not explicitly set in portal sketch | Not explicitly set in traced setup | I: normally 192.168.4.1 through SDK defaults; U: actual runtime IP was not measured. The explicit 192.168.4.1 in lesson 4 is not evidence that this portal sets it |
| Captive DNS / mDNS | No DNSServer, ESP8266mDNS, wildcard DNS or mDNS begin call in portal sources | No such activation found in the traced application setup/loop. `.local` exists at `0x4d410` in general network strings | V: inspected S files and stock setup `0x1e80–0x2015`, loop `0x5084–0x51e3`; I: the stock AP should not be described as a confirmed captive portal or mDNS responder from strings alone |
| Serial | Debug disabled by macro; if enabled, 9600 baud; sensor uses GPIO1/3 | Initialises serial at 9600 and prints Q1 mini banner | V: S/0.SpiderBot.ino:22–39,55–60; flash `0x1e8b` loads `0x2580`=9600 from `0x1e68`, call at `0x1ea4`, banner print `0x1ea7–0x1eac`. I: baseline's silence at 115200 does not imply no sketch serial output |
| Servo library and pulse range | Arduino Servo.h, attach min/max 400/2400 µs; converts nominal 1–180 to 400–2400 before Servo.write | Same conversion constants and attach limits; waveform-based library with 20,000 µs refresh constant | V: S/actionarray.h:8–13; S/spiderbot_motion.cpp:39–40,133–140; flash `0x1321–0x132e`, `0x1eb2–0x1f17`, library `0x5298–0x52fb`. I: stock is compatible with the ESP8266 core Servo implementation, not a bundled independent servo library |
| Motion servicing | 10 ms interpolation; HTTP servicing and stop checks during motion | 10 ms interpolation, no client servicing/stop check inside runner | V: S/spiderbot_motion.cpp:63–114; stock `0x1420–0x14fe`. I: stock HTTP control blocks during table playback; vendor stop API can interrupt between samples |
| Extra hardware | IR on D3 (GPIO10 only under ESP32-C3 conditional); ultrasonic TX/RX, 40 cm threshold, 30 ms timeout, avoidance loop | No IR/ultrasonic setup or avoidance dispatch in the traced stock application entry points | V: S/0.SpiderBot.ino:8–27,55–59,115–170; stock setup/loop and programme dispatch. I: these are vendor additions relative to the observed stock application |
| Core / SDK / libraries | No pinned Arduino core or Servo/EEPROM/web-server versions; bundled IRremoteESP8266 declares 2.9.0 | SDK `2.2.2-dev(38a443e)`; vendor precompiled binary instead has `2.2.1(cfd48f3)` | V: S includes, archive inventory; IRremoteESP8266/library.properties:1–9; stock flash `0x4aee0`, vendor binary `0x55058`. U: exact Arduino core, toolchain and linked library versions for either image |

## Motion differences and binary location

**V:** Each stock keyframe is nine little-endian 32-bit integers: eight angle inputs followed by milliseconds. Tables occupy flash `[0x3fc9c,0x40de8)`, entirely inside the first application segment. Direct centre/zero arrays follow at `0x40de8` and `0x40e0c`. This is application-resident data, not EEPROM or a filesystem. Table bytes and count values were decoded directly; no hardware inference is needed to recover their numeric contents.

**I, high confidence:** The table-to-pm mapping is established by the dispatch routine: pointer/count-address pairs at `0x500c–0x5083`, loaded for pm branches, feed the common runner at virtual `0x40201420`. For example pm=8 loads the table address `0x4024050c` and count address `0x3ffe84e8`; that RAM count is initialised by flash `0x4aea0` to 1. This table matches vendor `Servo_Prg_8` exactly. Matching full angle sequences and UI labels corroborate the other assignments.

| pm / motion | Vendor → stock difference | Stock flash start | Status / source evidence |
|---|---|---|---|
| 1 Standby | 500→200 ms per row; final pose `60,90,90,120,120,90,90,60` → `70,90,90,110,110,90,90,70` | `0x40da0` | V: S/actionarray.h:28–33, 72 stock bytes |
| 2 Forward | 200→150 ms per row; vendor row 5/6 columns 1/2 =39/141 versus stock 45/135; row 7 column 2 =141 versus stock 135 | `0x40c14` | V: S/actionarray.h:36–50, 396 stock bytes |
| 3 Backward | All angles identical; 200→150 ms | `0x40a88` | V: S/actionarray.h:53–67, all 11 eight-angle rows byte-match |
| 4 Left shift | All angles identical; 200→150 ms | `0x408fc` | V: S/actionarray.h:70–84, all 11 eight-angle rows byte-match |
| 5 Right shift | All angles identical; 200→150 ms | `0x40770` | V: S/actionarray.h:87–101, all 11 eight-angle rows byte-match |
| 6 Turn left | All angles identical; 200→150 ms | `0x40650` | V: S/actionarray.h:104–115, all 8 eight-angle rows byte-match |
| 7 Turn right | All angles identical; 200→150 ms | `0x40530` | V: S/actionarray.h:118–129, all 8 eight-angle rows byte-match |
| 8 Lie | No difference: all eight angles and 500 ms match | `0x4050c` | V: S/actionarray.h:132–136; complete 36-byte match |
| 9 Say Hi | Vendor 10 rows wave columns 4/5 from tilted support, mixed 220–280 ms; stock 7 rows wave columns 0/1, all 400 ms; neither has an implicit repeat count | `0x40410` | V: S/actionarray.h:138–157, 252 stock bytes; full tables in motion document |
| 10 Fighting | Vendor first 11 angle rows match stock; vendor adds final standby row 12; 500→375 ms | `0x40284` | V: S/actionarray.h:160–175, 396 stock bytes |
| 11 Push up | Vendor 13 rows versus stock 11; additional down/up pair and different up/start angles; timing schedule also differs | `0x400f8` | V: S/actionarray.h:178–194, 396 stock bytes; full schedules in motion document |
| 12 Sleep | Both angle rows identical; 500→375 ms | `0x400b0` | V: S/actionarray.h:197–202, both eight-angle rows byte-match |
| 13 Dance 1 | All angles identical; 400→300 ms | `0x3ff48` | V: S/actionarray.h:205–218, all 10 eight-angle rows byte-match |
| 14 Dance 2 | Vendor 10 rows, 95/85 inward bend targets and 260/320 ms; stock 9 rows, 115/65 targets, final 75/105 partial pose, all 300 ms | `0x3fe04` | V: S/actionarray.h:221–234, 324 stock bytes; full tables in motion document |
| 15 Dance 3 | All angles identical; 400→300 ms | `0x3fc9c` | V: S/actionarray.h:237–250, all 10 eight-angle rows byte-match |

**V:** Eight tables (pm=3–7,12,13,15) match every angle row in order but differ in timing. Lie is the only complete matching table. Vendor Say Hi appends Standby after its own table, as stock does; Sleep prepends Standby in both (S/spiderbot_motion.cpp:246–269; stock flash `0x5169–0x517a`, `0x5191–0x51a2`). Every other ordinary pm=1–15 request executes its table once. The motion document includes every row in both revisions, exact timings, direct-pose delays, additional programme behaviour and interpolation limitations relevant to re-authoring.

## Calibration and pin-order details

**V:** Stock EEPROM construction at flash `0x5494–0x54ad` computes `(0x405fb000 - 0x40200000) >> 12 = 0x3fb`; begin(512) reads that sector. Bytes 0–7 are raw signed two's-complement trim values in logical servo order. The rest of the 512-byte buffer is not assigned an application-level format by the analysed code. Writes commit through the EEPROM library, which erases the sector and writes the buffer (`0x542c–0x5487`). The entire dumped sector is FF; therefore the signed trim read path supplies **-1**, not an automatically zeroed default. Vendor similarly reads raw signed EEPROM bytes without initialising erased data (S/spiderbot_motion.cpp:39; S/webserver.cpp:618–635).

**V:** The stock pin discrepancy is between actual attachment calls, not merely conflicting comments. Logical channel 0 selects the Servo object at RAM `0x3fff0660` (`0x1361`), attached to GPIO15 at startup (`0x1eb2–0x1ebc`) and GPIO16 after save (`0x3740–0x3749`). After save the complete logical order is `16,5,4,2,14,12,13,15`; this also disagrees with the page labels. The other channels are traced in the same way. The stock `/save` handler explicitly detaches all channels first, so the subsequent attach calls can change their pins. **I:** Even an invalid-value save request that reaches the detach/reattach path can change the mapping; changing calibration is not required for that side effect. No physical leg assignment has been tested.

**V:** `Servo_Offset = {-2,-9,-5,-3,0,-12,5,5}` exists in vendor actionarray.h:17, but has no use in the complete portal source set; the writer uses EEPROM instead. It must not be treated as a default calibration table. Vendor actionarray.h:140–143 describes right-front/right-rear/left-front/left-rear parts; **U:** whether those descriptions match this particular clone's physical assembly. Baseline page defaults are editor values, not evidence of startup attachment or a saved calibration.

## Application images, library evidence and optional build

**V:** Parsing the ESP8266 E9 headers yields these segment layouts. RAM contents quoted above are initialised from their corresponding file ranges.

| Image | Data flash/file offset | Load address | Length |
|---|---|---|---|
| Stock app segment 0 | `0x01010` | `0x40201010` | `0x42c98` |
| Stock app segment 1 | `0x43cb0` | `0x40100000` | `0x104` |
| Stock app segment 2 | `0x43dbc` | `0x40100104` | `0x6bf4` |
| Stock app segment 3 | `0x4a9b8` | `0x3ffe8000` | `0x520` |
| Stock app segment 4 | `0x4aee0` | `0x3ffe8530` | `0x25d8` |
| Vendor app segment 0 | `0x01010` | `0x40201010` | `0x4c970` |
| Vendor app segment 1 | `0x4d988` | `0x40100000` | `0x70d0` |
| Vendor app segment 2 | `0x54a60` | `0x3ffe8000` | `0x5f0` |
| Vendor app segment 3 | `0x55058` | `0x3ffe8600` | `0x104c` |

**V:** Stock entry is `0x401000b8`, vendor entry `0x401066d0`. Vendor binary length is 352,432 bytes (`0x560b0`); it includes a bootloader at offset zero and an application at `0x1000`. It is not a full 4 MB dump and does not appear as a contiguous byte sequence in stock. Stock app segment data ends at `0x4d4b8`; the requested comparison region through `0x4dfff` also includes checksum/alignment/padding. Different segment lengths and keyframe contents rule out identical application images independently of strings.

**V:** Stock Servo library code uses `_servoMap`-style pin bit bookkeeping, rounded `improved_map`, waveform output and the 20 ms refresh constant (`0x51e8–0x53ca`). Official [Arduino core 2.7.4 Servo.cpp](https://github.com/esp8266/Arduino/blob/2.7.4/libraries/Servo/src/Servo.cpp), locally retained as `references/lafvin/core-2.7.4-servo.cpp`, contains these features at lines 27–40,84–114; [2.5.2 Servo.cpp](https://github.com/esp8266/Arduino/blob/2.5.2/libraries/Servo/src/Servo.cpp), retained alongside it, lacks the pin bit bookkeeping in detach/write. **I:** 2.7.4 is a plausible comparison core, not an identified exact release. The [official EEPROM source](https://github.com/esp8266/Arduino/blob/2.7.4/libraries/EEPROM/EEPROM.cpp):34–49 corroborates the linker-derived sector format. U: SDK version alone does not identify the Arduino release or every library revision.

**V:** A read-only toolchain download attempt from PlatformIO's registry returned HTTP 403 for `toolchain-xtensa` 2.100300.220621; registry metadata is retained as `references/lafvin/toolchain-registry.json`. The existing older ESP32 objdump crashed when used on these raw segments. An existing newer `toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-objdump` successfully decoded the common Xtensa instructions; branch-target-aligned disassembly was used to avoid losing alignment across literal pools and padding. **U:** This is not a complete symbolic decompilation or an instruction-for-instruction comparison of every application path.

**I:** A fresh vendor build would add little provenance evidence without the original core, flags and linker settings: the vendor already supplies a binary whose complete motion tables match the downloaded source, and that binary has a different SDK and portal from stock. No vendor rebuild was attempted; no vendor build file was run. `build/lafvin-build/` was created for the unsuccessful trusted toolchain retrieval and remains otherwise empty. The 403 did not block motion extraction or the comparative findings.

## Licence and Q1 mini attribution

**V:** No licence, copyright notice, Jason Workshop attribution or Q1 mini reference was found in any non-IR top-level lesson `.ino` or robot `.h`/`.cpp` source; the six complete portal source files were included in that literal search. The only archive file named LICENSE/COPYING is `IRremoteESP8266/LICENSE.txt`, containing GNU LGPL 2.1 at lines 2–3. That library's properties credit David Conran, Sebastien Warin, Mark Szabo and Ken Shirriff (library.properties:3), and declare version 2.9.0 (line 2).

**U:** The robot sketch/keyframe licence is not stated in the supplied bundle. The IR library's licence is not evidence of a licence grant over the unrelated robot motion tables. **V:** Stock identifies itself as Q1 mini in its banner at flash `0x4b036`; **I:** this supports Q1-family ancestry, but does not establish Jason Workshop authorship of this specific ESP8266 port. The baseline's Jason Workshop references are contextual leads, not vendor attribution discovered in this bundle.

## Remaining unknowns and practical limits

- **U:** Exact stock source revision, original author/porter, date, build flags, Arduino core/toolchain release and exact linked Servo/EEPROM/network library versions. No exact source-to-binary reproduction was obtained.
- **U:** Runtime AP IP, actual DNS/mDNS services and behaviour on a client device. Static setup/loop analysis provides no confirmation of captive DNS or mDNS activation; `.local` alone is insufficient.
- **U:** Physical servo-to-leg orientation and mechanical limits of this clone. Numeric keyframes are verified; physical motion names and direction depend on assembly, startup/save pin state and trim.
- **I:** The matching eight full angle sequences and common runner support shared lineage strongly; they cannot distinguish direct copying from a common upstream ancestor or identify who made each change.
- **V:** No serial port or robot hardware access occurred. All source frames, stock frames, times and repeat behaviour needed for gesture re-authoring are documented, including numeric differences rather than assuming that similarly named motions are equal.
