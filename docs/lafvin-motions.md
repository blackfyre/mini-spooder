# LAFVIN and stock motion keyframes

## Evidence and units

**Verified (V)** means directly read from the cited source or flash bytes. **Inferred (I)** means a semantic interpretation or predicted behaviour, not a hardware observation. All tables below are V: vendor rows are transcribed from `references/lafvin/extracted/0.SpiderBot/actionarray.h`; stock rows are decoded directly as nine little-endian signed 32-bit integers per 36-byte row. Their programme identities are inferred with high confidence from the stock dispatch at flash `0x5084–0x51e3`, its pointer pairs at `0x500c–0x5083`, the referenced step counts, and matching pose sequences. All offsets are absolute offsets into the full flash dump, not RAM addresses.

V: Each row is eight nominal angle targets followed by transition time in milliseconds. These are input angles, before signed calibration and pulse conversion. Vendor array column labels are GPIO `14,12,13,15,16,5,4,2` (actionarray.h:16,30); stock physical wiring changes after `/save`, as detailed below. Do not assign these columns to hexapod joints without re-authoring.

V: `motion-evidence.json` beside the archive records every row, source row line, stock offset/count pointer, vendor binary offset and SHA-256 for each table. Every vendor motion table also occurs byte-for-byte in the supplied `BIN/SpiderBot.ino.bin`; only pm=8 occurs as a complete byte-identical table in stock.

## Playback, delays and repeats

V (vendor spiderbot_motion.cpp:63–114; stock flash `0x1420–0x14fe`): Each table is executed once, in listed order. There is no separate repeat-count field and no additional per-row hold delay. Repeated waves, push-ups and dance cycles are explicitly expanded into rows. The ninth integer controls interpolation from the cached previous pose, not a hold after immediately setting a pose.

V: Both runners use a 10 ms interpolation interval and integer `T/10` samples with loop index starting at zero. V: Targets are copied to the pose cache after each row; the runner does not explicitly write the exact endpoint, and equal-angle channels are not written in that loop. I: Consequently final physical commands can fall slightly short of the targets, and cached state can differ from the last pulse sent. Do not reproduce that artefact when re-authoring gestures.

I (arithmetic from the verified loops): Scheduled delay per row is `10 * floor(T/10)` ms plus execution overhead, not necessarily T; for example 375→370, 525→520 and 1125→1120 ms. Web servicing adds variable overhead in the vendor runner (spiderbot_motion.cpp:78–84); stock has no HTTP servicing call inside its interpolation loop. All durations below are nominal sums of table values, before that quantisation and overhead.

V: pm=9 plays its own table then pm=1 once; pm=12 plays pm=1 once then its own table. Vendor: spiderbot_motion.cpp:246–269. Stock: flash `0x5169–0x517a`, `0x5191–0x51a2`. Every other pm=1–15 plays its own table once. Stock clears the requested programme afterwards (`0x51d6–0x51d9`); vendor does likewise (0.SpiderBot.ino:96–99). No autonomous walking repeat is implied by a single HTTP pm request.

V: Vendor additional programmes: 21 = obstacle avoidance, repeatedly calling forward or two left turns based on distance (0.SpiderBot.ino:135–170); 99 = direct centre pose; 100 = direct zero pose. Programmes 99/100 write eight channels sequentially with 10 ms delays and then delay 300 ms (spiderbot_motion.cpp:20–33,116–127,286–296). Stock dispatch supports 99/100 (`0x5106–0x5115`, `0x51c4–0x51d3`) but not 21. I: Direct-pose scheduled delays total 380 ms, excluding computation.

## Servo column mapping and direct poses

V: The stock web-page GPIO labels agree with the vendor map, but stock startup attach calls (`0x1eb2–0x1f17`) disagree. `/save` detaches all eight (`0x369e–0x36d3`) and reattaches in a third order, `16,5,4,2,14,12,13,15` (`0x373d–0x37a2`), which also disagrees with the labels. The vendor always attaches in labelled order (spiderbot_motion.cpp:129–140).

| Column | Vendor / page label GPIO | Stock startup GPIO | Stock after save GPIO |
|---|---|---|---|
| 0 | 14 | 15 | 16 |
| 1 | 12 | 13 | 5 |
| 2 | 13 | 12 | 4 |
| 3 | 15 | 14 | 2 |
| 4 | 16 | 2 | 14 |
| 5 | 5 | 4 | 12 |
| 6 | 4 | 5 | 13 |
| 7 | 2 | 16 | 15 |

V: Vendor zero targets are `[135,45,135,45,45,135,45,135]` with stored metadata 500 ms (actionarray.h:21). Vendor centre targets are `[60,90,90,120,120,90,90,60]`, metadata 500 ms (line 25). Stock direct zero (`0x40e0c`) **and centre** (`0x40de8`) both store `[135,45,135,45,45,135,45,135,200]`. Their direct writers ignore the ninth integer. Stock standby is instead `[70,90,90,110,110,90,90,70]` in pm=1 row 2 (`0x40dc4`).

V: Vendor `Servo_Offset` is `[-2,-9,-5,-3,0,-12,5,5]` (actionarray.h:17), but no portal-sketch code uses it: actual trim comes from EEPROM bytes 0–7 (spiderbot_motion.cpp:39). Stock also adds a signed EEPROM byte (`0x1318–0x1326`). The erased sector therefore corresponds to -1 per channel, not zero, on this code path; no initialisation-to-zero branch exists in the analysed startup. V: Sector `0x3fb000–0x3fbfff` is entirely FF.

## Programme inventory

| pm | Name | Vendor rows / nominal ms | Stock rows / nominal ms | Same angle rows in order? | Stock start |
|---|---|---|---|---|---|
| 1 | Standby | 2 / 1000 | 2 / 400 | No | `0x40da0` |
| 2 | Forward | 11 / 2200 | 11 / 1650 | No | `0x40c14` |
| 3 | Backward | 11 / 2200 | 11 / 1650 | Yes | `0x40a88` |
| 4 | Left shift | 11 / 2200 | 11 / 1650 | Yes | `0x408fc` |
| 5 | Right shift | 11 / 2200 | 11 / 1650 | Yes | `0x40770` |
| 6 | Turn left | 8 / 1600 | 8 / 1200 | Yes | `0x40650` |
| 7 | Turn right | 8 / 1600 | 8 / 1200 | Yes | `0x40530` |
| 8 | Lie | 1 / 500 | 1 / 500 | Yes | `0x4050c` |
| 9 | Say Hi | 10 / 2500 | 7 / 2800 | No | `0x40410` |
| 10 | Fighting | 12 / 6000 | 11 / 4125 | No | `0x40284` |
| 11 | Push up | 13 / 12700 | 11 / 7200 | No | `0x400f8` |
| 12 | Sleep | 2 / 1000 | 2 / 750 | Yes | `0x400b0` |
| 13 | Dance 1 | 10 / 4000 | 10 / 3000 | Yes | `0x3ff48` |
| 14 | Dance 2 | 10 / 2720 | 9 / 2700 | No | `0x3fe04` |
| 15 | Dance 3 | 10 / 4000 | 10 / 3000 | Yes | `0x3fc9c` |

V: Inventory durations cover each named table only. Add a standby table for the complete pm=9 or pm=12 command: vendor +1000 ms; stock +400 ms.

## pm=1: Standby

V: Vendor `Servo_Prg_1` begins at actionarray.h:29; complete table byte match in vendor binary at `0x48664`. Stock begins at `0x40da0`; 2 rows, count stored at `0x4aebc`, pointer pair at `0x500c`.

V: Angle data and/or the number of rows differ. Both complete tables follow.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 500 | actionarray.h:31 |
| 2 | 60 | 90 | 90 | 120 | 120 | 90 | 90 | 60 | 500 | actionarray.h:32 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 200 | `0x40da0` |
| 2 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | `0x40dc4` |

## pm=2: Forward

V: Vendor `Servo_Prg_2` begins at actionarray.h:37; complete table byte match in vendor binary at `0x484d8`. Stock begins at `0x40c14`; 11 rows, count stored at `0x4aeb8`, pointer pair at `0x5014`.

V: Angle data and/or the number of rows differ. Both complete tables follow.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:39 |
| 2 | 90 | 90 | 90 | 110 | 110 | 90 | 45 | 90 | 200 | actionarray.h:40 |
| 3 | 70 | 90 | 90 | 110 | 110 | 90 | 45 | 70 | 200 | actionarray.h:41 |
| 4 | 70 | 90 | 90 | 90 | 90 | 90 | 45 | 70 | 200 | actionarray.h:42 |
| 5 | 70 | 39 | 141 | 90 | 90 | 90 | 90 | 70 | 200 | actionarray.h:43 |
| 6 | 70 | 39 | 141 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:44 |
| 7 | 90 | 90 | 141 | 110 | 110 | 90 | 90 | 90 | 200 | actionarray.h:45 |
| 8 | 90 | 90 | 90 | 110 | 110 | 135 | 90 | 90 | 200 | actionarray.h:46 |
| 9 | 70 | 90 | 90 | 110 | 110 | 135 | 90 | 70 | 200 | actionarray.h:47 |
| 10 | 70 | 90 | 90 | 110 | 90 | 135 | 90 | 70 | 200 | actionarray.h:48 |
| 11 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:49 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x40c14` |
| 2 | 90 | 90 | 90 | 110 | 110 | 90 | 45 | 90 | 150 | `0x40c38` |
| 3 | 70 | 90 | 90 | 110 | 110 | 90 | 45 | 70 | 150 | `0x40c5c` |
| 4 | 70 | 90 | 90 | 90 | 90 | 90 | 45 | 70 | 150 | `0x40c80` |
| 5 | 70 | 45 | 135 | 90 | 90 | 90 | 90 | 70 | 150 | `0x40ca4` |
| 6 | 70 | 45 | 135 | 110 | 110 | 90 | 90 | 70 | 150 | `0x40cc8` |
| 7 | 90 | 90 | 135 | 110 | 110 | 90 | 90 | 90 | 150 | `0x40cec` |
| 8 | 90 | 90 | 90 | 110 | 110 | 135 | 90 | 90 | 150 | `0x40d10` |
| 9 | 70 | 90 | 90 | 110 | 110 | 135 | 90 | 70 | 150 | `0x40d34` |
| 10 | 70 | 90 | 90 | 110 | 90 | 135 | 90 | 70 | 150 | `0x40d58` |
| 11 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x40d7c` |

## pm=3: Backward

V: Vendor `Servo_Prg_3` begins at actionarray.h:54; complete table byte match in vendor binary at `0x4834c`. Stock begins at `0x40a88`; 11 rows, count stored at `0x4aeb4`, pointer pair at `0x501c`.

V: All eight angle columns match stock in every row; the times differ. Both tables are printed to preserve the timings.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:56 |
| 2 | 90 | 45 | 90 | 110 | 110 | 90 | 90 | 90 | 200 | actionarray.h:57 |
| 3 | 70 | 45 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:58 |
| 4 | 70 | 45 | 90 | 90 | 90 | 90 | 90 | 70 | 200 | actionarray.h:59 |
| 5 | 70 | 90 | 90 | 90 | 90 | 135 | 45 | 70 | 200 | actionarray.h:60 |
| 6 | 70 | 90 | 90 | 110 | 110 | 135 | 45 | 70 | 200 | actionarray.h:61 |
| 7 | 90 | 90 | 90 | 110 | 110 | 135 | 90 | 90 | 200 | actionarray.h:62 |
| 8 | 90 | 90 | 135 | 110 | 110 | 90 | 90 | 90 | 200 | actionarray.h:63 |
| 9 | 70 | 90 | 135 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:64 |
| 10 | 70 | 90 | 135 | 90 | 110 | 90 | 90 | 70 | 200 | actionarray.h:65 |
| 11 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:66 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x40a88` |
| 2 | 90 | 45 | 90 | 110 | 110 | 90 | 90 | 90 | 150 | `0x40aac` |
| 3 | 70 | 45 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x40ad0` |
| 4 | 70 | 45 | 90 | 90 | 90 | 90 | 90 | 70 | 150 | `0x40af4` |
| 5 | 70 | 90 | 90 | 90 | 90 | 135 | 45 | 70 | 150 | `0x40b18` |
| 6 | 70 | 90 | 90 | 110 | 110 | 135 | 45 | 70 | 150 | `0x40b3c` |
| 7 | 90 | 90 | 90 | 110 | 110 | 135 | 90 | 90 | 150 | `0x40b60` |
| 8 | 90 | 90 | 135 | 110 | 110 | 90 | 90 | 90 | 150 | `0x40b84` |
| 9 | 70 | 90 | 135 | 110 | 110 | 90 | 90 | 70 | 150 | `0x40ba8` |
| 10 | 70 | 90 | 135 | 90 | 110 | 90 | 90 | 70 | 150 | `0x40bcc` |
| 11 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x40bf0` |

## pm=4: Left shift

V: Vendor `Servo_Prg_4` begins at actionarray.h:71; complete table byte match in vendor binary at `0x481c0`. Stock begins at `0x408fc`; 11 rows, count stored at `0x4aeb0`, pointer pair at `0x5024`.

V: All eight angle columns match stock in every row; the times differ. Both tables are printed to preserve the timings.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:73 |
| 2 | 70 | 90 | 45 | 90 | 90 | 90 | 90 | 70 | 200 | actionarray.h:74 |
| 3 | 70 | 90 | 45 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:75 |
| 4 | 90 | 90 | 45 | 110 | 110 | 90 | 90 | 90 | 200 | actionarray.h:76 |
| 5 | 90 | 135 | 90 | 110 | 110 | 45 | 90 | 90 | 200 | actionarray.h:77 |
| 6 | 70 | 135 | 90 | 110 | 110 | 45 | 90 | 70 | 200 | actionarray.h:78 |
| 7 | 70 | 135 | 90 | 90 | 90 | 90 | 90 | 70 | 200 | actionarray.h:79 |
| 8 | 70 | 90 | 90 | 90 | 90 | 90 | 135 | 70 | 200 | actionarray.h:80 |
| 9 | 70 | 90 | 90 | 110 | 110 | 90 | 135 | 70 | 200 | actionarray.h:81 |
| 10 | 70 | 90 | 90 | 110 | 110 | 90 | 135 | 90 | 200 | actionarray.h:82 |
| 11 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:83 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x408fc` |
| 2 | 70 | 90 | 45 | 90 | 90 | 90 | 90 | 70 | 150 | `0x40920` |
| 3 | 70 | 90 | 45 | 110 | 110 | 90 | 90 | 70 | 150 | `0x40944` |
| 4 | 90 | 90 | 45 | 110 | 110 | 90 | 90 | 90 | 150 | `0x40968` |
| 5 | 90 | 135 | 90 | 110 | 110 | 45 | 90 | 90 | 150 | `0x4098c` |
| 6 | 70 | 135 | 90 | 110 | 110 | 45 | 90 | 70 | 150 | `0x409b0` |
| 7 | 70 | 135 | 90 | 90 | 90 | 90 | 90 | 70 | 150 | `0x409d4` |
| 8 | 70 | 90 | 90 | 90 | 90 | 90 | 135 | 70 | 150 | `0x409f8` |
| 9 | 70 | 90 | 90 | 110 | 110 | 90 | 135 | 70 | 150 | `0x40a1c` |
| 10 | 70 | 90 | 90 | 110 | 110 | 90 | 135 | 90 | 150 | `0x40a40` |
| 11 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x40a64` |

## pm=5: Right shift

V: Vendor `Servo_Prg_5` begins at actionarray.h:88; complete table byte match in vendor binary at `0x48034`. Stock begins at `0x40770`; 11 rows, count stored at `0x4aeac`, pointer pair at `0x502c`.

V: All eight angle columns match stock in every row; the times differ. Both tables are printed to preserve the timings.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:90 |
| 2 | 70 | 90 | 90 | 90 | 90 | 45 | 90 | 70 | 200 | actionarray.h:91 |
| 3 | 70 | 90 | 90 | 110 | 110 | 45 | 90 | 70 | 200 | actionarray.h:92 |
| 4 | 90 | 90 | 90 | 110 | 110 | 45 | 90 | 90 | 200 | actionarray.h:93 |
| 5 | 90 | 90 | 45 | 110 | 110 | 90 | 135 | 90 | 200 | actionarray.h:94 |
| 6 | 70 | 90 | 45 | 110 | 110 | 90 | 135 | 70 | 200 | actionarray.h:95 |
| 7 | 70 | 90 | 90 | 90 | 90 | 90 | 135 | 70 | 200 | actionarray.h:96 |
| 8 | 70 | 135 | 90 | 90 | 90 | 90 | 90 | 70 | 200 | actionarray.h:97 |
| 9 | 70 | 135 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:98 |
| 10 | 90 | 135 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:99 |
| 11 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:100 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x40770` |
| 2 | 70 | 90 | 90 | 90 | 90 | 45 | 90 | 70 | 150 | `0x40794` |
| 3 | 70 | 90 | 90 | 110 | 110 | 45 | 90 | 70 | 150 | `0x407b8` |
| 4 | 90 | 90 | 90 | 110 | 110 | 45 | 90 | 90 | 150 | `0x407dc` |
| 5 | 90 | 90 | 45 | 110 | 110 | 90 | 135 | 90 | 150 | `0x40800` |
| 6 | 70 | 90 | 45 | 110 | 110 | 90 | 135 | 70 | 150 | `0x40824` |
| 7 | 70 | 90 | 90 | 90 | 90 | 90 | 135 | 70 | 150 | `0x40848` |
| 8 | 70 | 135 | 90 | 90 | 90 | 90 | 90 | 70 | 150 | `0x4086c` |
| 9 | 70 | 135 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x40890` |
| 10 | 90 | 135 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x408b4` |
| 11 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x408d8` |

## pm=6: Turn left

V: Vendor `Servo_Prg_6` begins at actionarray.h:105; complete table byte match in vendor binary at `0x47f14`. Stock begins at `0x40650`; 8 rows, count stored at `0x4aea8`, pointer pair at `0x5034`.

V: All eight angle columns match stock in every row; the times differ. Both tables are printed to preserve the timings.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:107 |
| 2 | 90 | 90 | 90 | 110 | 110 | 90 | 90 | 90 | 200 | actionarray.h:108 |
| 3 | 90 | 135 | 90 | 110 | 110 | 90 | 135 | 90 | 200 | actionarray.h:109 |
| 4 | 70 | 135 | 90 | 110 | 110 | 90 | 135 | 70 | 200 | actionarray.h:110 |
| 5 | 70 | 135 | 90 | 90 | 90 | 90 | 135 | 70 | 200 | actionarray.h:111 |
| 6 | 70 | 135 | 135 | 90 | 90 | 135 | 135 | 70 | 200 | actionarray.h:112 |
| 7 | 70 | 135 | 135 | 110 | 110 | 135 | 135 | 70 | 200 | actionarray.h:113 |
| 8 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:114 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x40650` |
| 2 | 90 | 90 | 90 | 110 | 110 | 90 | 90 | 90 | 150 | `0x40674` |
| 3 | 90 | 135 | 90 | 110 | 110 | 90 | 135 | 90 | 150 | `0x40698` |
| 4 | 70 | 135 | 90 | 110 | 110 | 90 | 135 | 70 | 150 | `0x406bc` |
| 5 | 70 | 135 | 90 | 90 | 90 | 90 | 135 | 70 | 150 | `0x406e0` |
| 6 | 70 | 135 | 135 | 90 | 90 | 135 | 135 | 70 | 150 | `0x40704` |
| 7 | 70 | 135 | 135 | 110 | 110 | 135 | 135 | 70 | 150 | `0x40728` |
| 8 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x4074c` |

## pm=7: Turn right

V: Vendor `Servo_Prg_7` begins at actionarray.h:119; complete table byte match in vendor binary at `0x47df4`. Stock begins at `0x40530`; 8 rows, count stored at `0x4aea4`, pointer pair at `0x503c`.

V: All eight angle columns match stock in every row; the times differ. Both tables are printed to preserve the timings.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:121 |
| 2 | 70 | 90 | 90 | 90 | 90 | 90 | 90 | 70 | 200 | actionarray.h:122 |
| 3 | 70 | 90 | 45 | 90 | 90 | 45 | 90 | 70 | 200 | actionarray.h:123 |
| 4 | 70 | 90 | 45 | 110 | 110 | 45 | 90 | 70 | 200 | actionarray.h:124 |
| 5 | 90 | 90 | 45 | 110 | 110 | 45 | 90 | 90 | 200 | actionarray.h:125 |
| 6 | 90 | 45 | 45 | 110 | 110 | 45 | 45 | 90 | 200 | actionarray.h:126 |
| 7 | 70 | 45 | 45 | 110 | 110 | 45 | 45 | 70 | 200 | actionarray.h:127 |
| 8 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 200 | actionarray.h:128 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x40530` |
| 2 | 70 | 90 | 90 | 90 | 90 | 90 | 90 | 70 | 150 | `0x40554` |
| 3 | 70 | 90 | 45 | 90 | 90 | 45 | 90 | 70 | 150 | `0x40578` |
| 4 | 70 | 90 | 45 | 110 | 110 | 45 | 90 | 70 | 150 | `0x4059c` |
| 5 | 90 | 90 | 45 | 110 | 110 | 45 | 90 | 90 | 150 | `0x405c0` |
| 6 | 90 | 45 | 45 | 110 | 110 | 45 | 45 | 90 | 150 | `0x405e4` |
| 7 | 70 | 45 | 45 | 110 | 110 | 45 | 45 | 70 | 150 | `0x40608` |
| 8 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 150 | `0x4062c` |

## pm=8: Lie

V: Vendor `Servo_Prg_8` begins at actionarray.h:133; complete table byte match in vendor binary at `0x47dd0`. Stock begins at `0x4050c`; 1 rows, count stored at `0x4aea0`, pointer pair at `0x5044`.

V: The complete vendor table matches stock byte-for-byte (angles **and** time).

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 110 | 90 | 90 | 70 | 70 | 90 | 90 | 110 | 500 | actionarray.h:135 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 110 | 90 | 90 | 70 | 70 | 90 | 90 | 110 | 500 | `0x4050c` |

## pm=9: Say Hi

V: Vendor `Servo_Prg_9` begins at actionarray.h:145; complete table byte match in vendor binary at `0x47c68`. Stock begins at `0x40410`; 7 rows, count stored at `0x4ae9c`, pointer pair at `0x504c`.

V: Angle data and/or the number of rows differ. Both complete tables follow.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 60 | 90 | 125 | 80 | 120 | 82 | 90 | 50 | 280 | actionarray.h:147 |
| 2 | 60 | 90 | 125 | 80 | 92 | 74 | 90 | 50 | 220 | actionarray.h:148 |
| 3 | 60 | 90 | 125 | 80 | 38 | 62 | 90 | 50 | 240 | actionarray.h:149 |
| 4 | 60 | 90 | 125 | 80 | 34 | 118 | 90 | 50 | 260 | actionarray.h:150 |
| 5 | 60 | 90 | 125 | 80 | 46 | 58 | 90 | 50 | 260 | actionarray.h:151 |
| 6 | 60 | 90 | 125 | 80 | 34 | 118 | 90 | 50 | 260 | actionarray.h:152 |
| 7 | 60 | 90 | 125 | 80 | 46 | 58 | 90 | 50 | 260 | actionarray.h:153 |
| 8 | 60 | 90 | 125 | 80 | 44 | 88 | 90 | 50 | 220 | actionarray.h:154 |
| 9 | 60 | 90 | 125 | 80 | 92 | 78 | 90 | 50 | 220 | actionarray.h:155 |
| 10 | 60 | 90 | 125 | 80 | 120 | 82 | 90 | 50 | 280 | actionarray.h:156 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 400 | `0x40410` |
| 2 | 170 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 400 | `0x40434` |
| 3 | 170 | 130 | 90 | 90 | 90 | 90 | 90 | 90 | 400 | `0x40458` |
| 4 | 170 | 50 | 90 | 90 | 90 | 90 | 90 | 90 | 400 | `0x4047c` |
| 5 | 170 | 130 | 90 | 90 | 90 | 90 | 90 | 90 | 400 | `0x404a0` |
| 6 | 170 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 400 | `0x404c4` |
| 7 | 70 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 400 | `0x404e8` |

## pm=10: Fighting

V: Vendor `Servo_Prg_10` begins at actionarray.h:161; complete table byte match in vendor binary at `0x47ab8`. Stock begins at `0x40284`; 11 rows, count stored at `0x4ae98`, pointer pair at `0x5054`.

V: Angle data and/or the number of rows differ. Both complete tables follow.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 120 | 90 | 90 | 110 | 60 | 90 | 90 | 70 | 500 | actionarray.h:163 |
| 2 | 120 | 70 | 70 | 110 | 60 | 70 | 70 | 70 | 500 | actionarray.h:164 |
| 3 | 120 | 110 | 110 | 110 | 60 | 110 | 110 | 70 | 500 | actionarray.h:165 |
| 4 | 120 | 70 | 70 | 110 | 60 | 70 | 70 | 70 | 500 | actionarray.h:166 |
| 5 | 120 | 110 | 110 | 110 | 60 | 110 | 110 | 70 | 500 | actionarray.h:167 |
| 6 | 70 | 90 | 90 | 70 | 110 | 90 | 90 | 110 | 500 | actionarray.h:168 |
| 7 | 70 | 70 | 70 | 70 | 110 | 70 | 70 | 110 | 500 | actionarray.h:169 |
| 8 | 70 | 110 | 110 | 70 | 110 | 110 | 110 | 110 | 500 | actionarray.h:170 |
| 9 | 70 | 70 | 70 | 70 | 110 | 70 | 70 | 110 | 500 | actionarray.h:171 |
| 10 | 70 | 110 | 110 | 70 | 110 | 110 | 110 | 110 | 500 | actionarray.h:172 |
| 11 | 70 | 90 | 90 | 70 | 110 | 90 | 90 | 110 | 500 | actionarray.h:173 |
| 12 | 60 | 90 | 90 | 120 | 120 | 90 | 90 | 60 | 500 | actionarray.h:174 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 120 | 90 | 90 | 110 | 60 | 90 | 90 | 70 | 375 | `0x40284` |
| 2 | 120 | 70 | 70 | 110 | 60 | 70 | 70 | 70 | 375 | `0x402a8` |
| 3 | 120 | 110 | 110 | 110 | 60 | 110 | 110 | 70 | 375 | `0x402cc` |
| 4 | 120 | 70 | 70 | 110 | 60 | 70 | 70 | 70 | 375 | `0x402f0` |
| 5 | 120 | 110 | 110 | 110 | 60 | 110 | 110 | 70 | 375 | `0x40314` |
| 6 | 70 | 90 | 90 | 70 | 110 | 90 | 90 | 110 | 375 | `0x40338` |
| 7 | 70 | 70 | 70 | 70 | 110 | 70 | 70 | 110 | 375 | `0x4035c` |
| 8 | 70 | 110 | 110 | 70 | 110 | 110 | 110 | 110 | 375 | `0x40380` |
| 9 | 70 | 70 | 70 | 70 | 110 | 70 | 70 | 110 | 375 | `0x403a4` |
| 10 | 70 | 110 | 110 | 70 | 110 | 110 | 110 | 110 | 375 | `0x403c8` |
| 11 | 70 | 90 | 90 | 70 | 110 | 90 | 90 | 110 | 375 | `0x403ec` |

## pm=11: Push up

V: Vendor `Servo_Prg_11` begins at actionarray.h:179; complete table byte match in vendor binary at `0x478e4`. Stock begins at `0x400f8`; 11 rows, count stored at `0x4ae94`, pointer pair at `0x505c`.

V: Angle data and/or the number of rows differ. Both complete tables follow.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 120 | 120 | 90 | 90 | 70 | 500 | actionarray.h:181 |
| 2 | 100 | 90 | 90 | 80 | 80 | 90 | 90 | 100 | 600 | actionarray.h:182 |
| 3 | 60 | 90 | 90 | 120 | 120 | 90 | 90 | 60 | 700 | actionarray.h:183 |
| 4 | 100 | 90 | 90 | 80 | 80 | 90 | 90 | 100 | 800 | actionarray.h:184 |
| 5 | 60 | 90 | 90 | 120 | 120 | 90 | 90 | 60 | 900 | actionarray.h:185 |
| 6 | 100 | 90 | 90 | 80 | 80 | 90 | 90 | 100 | 1000 | actionarray.h:186 |
| 7 | 60 | 90 | 90 | 120 | 120 | 90 | 90 | 60 | 1100 | actionarray.h:187 |
| 8 | 100 | 90 | 90 | 80 | 80 | 90 | 90 | 100 | 2000 | actionarray.h:188 |
| 9 | 60 | 90 | 90 | 120 | 120 | 90 | 90 | 60 | 2500 | actionarray.h:189 |
| 10 | 135 | 90 | 90 | 45 | 45 | 90 | 90 | 135 | 200 | actionarray.h:190 |
| 11 | 70 | 90 | 90 | 45 | 60 | 90 | 90 | 135 | 800 | actionarray.h:191 |
| 12 | 70 | 90 | 90 | 45 | 110 | 90 | 90 | 135 | 800 | actionarray.h:192 |
| 13 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 800 | actionarray.h:193 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 375 | `0x400f8` |
| 2 | 100 | 90 | 90 | 80 | 80 | 90 | 90 | 100 | 450 | `0x4011c` |
| 3 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 525 | `0x40140` |
| 4 | 100 | 90 | 90 | 80 | 80 | 90 | 90 | 100 | 600 | `0x40164` |
| 5 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 675 | `0x40188` |
| 6 | 100 | 90 | 90 | 80 | 80 | 90 | 90 | 100 | 1125 | `0x401ac` |
| 7 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 1500 | `0x401d0` |
| 8 | 135 | 90 | 90 | 45 | 45 | 90 | 90 | 135 | 150 | `0x401f4` |
| 9 | 70 | 90 | 90 | 45 | 60 | 90 | 90 | 135 | 600 | `0x40218` |
| 10 | 70 | 90 | 90 | 45 | 110 | 90 | 90 | 135 | 600 | `0x4023c` |
| 11 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 600 | `0x40260` |

## pm=12: Sleep

V: Vendor `Servo_Prg_12` begins at actionarray.h:198; complete table byte match in vendor binary at `0x4789c`. Stock begins at `0x400b0`; 2 rows, count stored at `0x4ae90`, pointer pair at `0x5064`.

V: All eight angle columns match stock in every row; the times differ. Both tables are printed to preserve the timings.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 30 | 90 | 90 | 150 | 150 | 90 | 90 | 30 | 500 | actionarray.h:200 |
| 2 | 30 | 45 | 135 | 150 | 150 | 135 | 45 | 30 | 500 | actionarray.h:201 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 30 | 90 | 90 | 150 | 150 | 90 | 90 | 30 | 375 | `0x400b0` |
| 2 | 30 | 45 | 135 | 150 | 150 | 135 | 45 | 30 | 375 | `0x400d4` |

## pm=13: Dance 1

V: Vendor `Servo_Prg_13` begins at actionarray.h:206; complete table byte match in vendor binary at `0x47734`. Stock begins at `0x3ff48`; 10 rows, count stored at `0x4ae8c`, pointer pair at `0x506c`.

V: All eight angle columns match stock in every row; the times differ. Both tables are printed to preserve the timings.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 400 | actionarray.h:208 |
| 2 | 50 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 400 | actionarray.h:209 |
| 3 | 90 | 90 | 90 | 130 | 90 | 90 | 90 | 90 | 400 | actionarray.h:210 |
| 4 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 50 | 400 | actionarray.h:211 |
| 5 | 90 | 90 | 90 | 90 | 130 | 90 | 90 | 90 | 400 | actionarray.h:212 |
| 6 | 50 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 400 | actionarray.h:213 |
| 7 | 90 | 90 | 90 | 130 | 90 | 90 | 90 | 90 | 400 | actionarray.h:214 |
| 8 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 50 | 400 | actionarray.h:215 |
| 9 | 90 | 90 | 90 | 90 | 130 | 90 | 90 | 90 | 400 | actionarray.h:216 |
| 10 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 400 | actionarray.h:217 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 300 | `0x3ff48` |
| 2 | 50 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 300 | `0x3ff6c` |
| 3 | 90 | 90 | 90 | 130 | 90 | 90 | 90 | 90 | 300 | `0x3ff90` |
| 4 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 50 | 300 | `0x3ffb4` |
| 5 | 90 | 90 | 90 | 90 | 130 | 90 | 90 | 90 | 300 | `0x3ffd8` |
| 6 | 50 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 300 | `0x3fffc` |
| 7 | 90 | 90 | 90 | 130 | 90 | 90 | 90 | 90 | 300 | `0x40020` |
| 8 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 50 | 300 | `0x40044` |
| 9 | 90 | 90 | 90 | 90 | 130 | 90 | 90 | 90 | 300 | `0x40068` |
| 10 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 90 | 300 | `0x4008c` |

## pm=14: Dance 2

V: Vendor `Servo_Prg_14` begins at actionarray.h:222; complete table byte match in vendor binary at `0x475cc`. Stock begins at `0x3fe04`; 9 rows, count stored at `0x4ae88`, pointer pair at `0x5074`.

V: Angle data and/or the number of rows differ. Both complete tables follow.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 45 | 135 | 110 | 110 | 135 | 45 | 70 | 320 | actionarray.h:224 |
| 2 | 95 | 45 | 135 | 85 | 110 | 135 | 45 | 70 | 260 | actionarray.h:225 |
| 3 | 70 | 45 | 135 | 110 | 85 | 135 | 45 | 95 | 260 | actionarray.h:226 |
| 4 | 95 | 45 | 135 | 85 | 110 | 135 | 45 | 70 | 260 | actionarray.h:227 |
| 5 | 70 | 45 | 135 | 110 | 85 | 135 | 45 | 95 | 260 | actionarray.h:228 |
| 6 | 95 | 45 | 135 | 85 | 110 | 135 | 45 | 70 | 260 | actionarray.h:229 |
| 7 | 70 | 45 | 135 | 110 | 85 | 135 | 45 | 95 | 260 | actionarray.h:230 |
| 8 | 95 | 45 | 135 | 85 | 110 | 135 | 45 | 70 | 260 | actionarray.h:231 |
| 9 | 70 | 45 | 135 | 110 | 85 | 135 | 45 | 95 | 260 | actionarray.h:232 |
| 10 | 70 | 45 | 135 | 110 | 110 | 135 | 45 | 70 | 320 | actionarray.h:233 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 45 | 135 | 110 | 110 | 135 | 45 | 70 | 300 | `0x3fe04` |
| 2 | 115 | 45 | 135 | 65 | 110 | 135 | 45 | 70 | 300 | `0x3fe28` |
| 3 | 70 | 45 | 135 | 110 | 65 | 135 | 45 | 115 | 300 | `0x3fe4c` |
| 4 | 115 | 45 | 135 | 65 | 110 | 135 | 45 | 70 | 300 | `0x3fe70` |
| 5 | 70 | 45 | 135 | 110 | 65 | 135 | 45 | 115 | 300 | `0x3fe94` |
| 6 | 115 | 45 | 135 | 65 | 110 | 135 | 45 | 70 | 300 | `0x3feb8` |
| 7 | 70 | 45 | 135 | 110 | 65 | 135 | 45 | 115 | 300 | `0x3fedc` |
| 8 | 115 | 45 | 135 | 65 | 110 | 135 | 45 | 70 | 300 | `0x3ff00` |
| 9 | 75 | 45 | 135 | 105 | 110 | 135 | 45 | 70 | 300 | `0x3ff24` |

## pm=15: Dance 3

V: Vendor `Servo_Prg_15` begins at actionarray.h:238; complete table byte match in vendor binary at `0x47464`. Stock begins at `0x3fc9c`; 10 rows, count stored at `0x4ae84`, pointer pair at `0x507c`.

V: All eight angle columns match stock in every row; the times differ. Both tables are printed to preserve the timings.

### Vendor

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 45 | 45 | 110 | 110 | 135 | 135 | 70 | 400 | actionarray.h:240 |
| 2 | 110 | 45 | 45 | 60 | 70 | 135 | 135 | 70 | 400 | actionarray.h:241 |
| 3 | 70 | 45 | 45 | 110 | 110 | 135 | 135 | 70 | 400 | actionarray.h:242 |
| 4 | 110 | 45 | 45 | 110 | 70 | 135 | 135 | 120 | 400 | actionarray.h:243 |
| 5 | 70 | 45 | 45 | 110 | 110 | 135 | 135 | 70 | 400 | actionarray.h:244 |
| 6 | 110 | 45 | 45 | 60 | 70 | 135 | 135 | 70 | 400 | actionarray.h:245 |
| 7 | 70 | 45 | 45 | 110 | 110 | 135 | 135 | 70 | 400 | actionarray.h:246 |
| 8 | 110 | 45 | 45 | 110 | 70 | 135 | 135 | 120 | 400 | actionarray.h:247 |
| 9 | 70 | 45 | 45 | 110 | 110 | 135 | 135 | 70 | 400 | actionarray.h:248 |
| 10 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 400 | actionarray.h:249 |

### Stock

| Step | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | ms | Evidence |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 70 | 45 | 45 | 110 | 110 | 135 | 135 | 70 | 300 | `0x3fc9c` |
| 2 | 110 | 45 | 45 | 60 | 70 | 135 | 135 | 70 | 300 | `0x3fcc0` |
| 3 | 70 | 45 | 45 | 110 | 110 | 135 | 135 | 70 | 300 | `0x3fce4` |
| 4 | 110 | 45 | 45 | 110 | 70 | 135 | 135 | 120 | 300 | `0x3fd08` |
| 5 | 70 | 45 | 45 | 110 | 110 | 135 | 135 | 70 | 300 | `0x3fd2c` |
| 6 | 110 | 45 | 45 | 60 | 70 | 135 | 135 | 70 | 300 | `0x3fd50` |
| 7 | 70 | 45 | 45 | 110 | 110 | 135 | 135 | 70 | 300 | `0x3fd74` |
| 8 | 110 | 45 | 45 | 110 | 70 | 135 | 135 | 120 | 300 | `0x3fd98` |
| 9 | 70 | 45 | 45 | 110 | 110 | 135 | 135 | 70 | 300 | `0x3fdbc` |
| 10 | 70 | 90 | 90 | 110 | 110 | 90 | 90 | 70 | 300 | `0x3fde0` |
