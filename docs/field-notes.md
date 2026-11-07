# Field notes

What testing on the real robot and the TrimUI turned up, in the order it happened, with the evidence for each finding. Dates are 2026-11-07.

## Reading the stock firmware

- **Back up first.** The full 4 MB flash was read twice with esptool, and both reads were byte-identical (`baseline/original-flash-4MB.bin`, SHA-256 in `baseline/README.md`). The vendor offers no support, so this image is the only way back (`mise run restore`).
- **Strings are not enough.** The first analysis, from readable strings only, got several things wrong. The stock web pages label the servos with GPIO 14, 12, 13, 15, 16, 5, 4, 2, but disassembling the attach calls showed the firmware really uses **15, 13, 12, 14, 2, 4, 5, 16** at start-up. It also re-attaches the servos in a third order after any save on the calibration page, which is a stock defect. Other corrections:
  - there's no captive portal (no DNS server is started);
  - `/online` plays a single keyframe;
  - the `pms` parameter is never handled;
  - the console runs at 9600 baud, not 115200.
- **Calibration exists.** The stock calibration page stores one signed trim byte per servo in the EEPROM emulation sector at 0x3FB000. On this robot that sector was erased, so every trim reads as −1.
- **Vendor source:** the LAFVIN download shares the motion code with the stock image, but it is a different revision. Only one motion table, Lie, is identical. `docs/lafvin-comparison.md` has the evidence. The motion tables in this project come from the stock image, not from the vendor source.

## Power

- **USB powers the logic, not the servos.** On USB alone the servos stayed limp whatever the firmware did, so the first "boot still" check proved nothing. It had to be repeated on battery. With USB plugged in, the power switch only cuts the battery, so a power cycle produces no `reset power-on` on the console.
- **Low battery is silent.** When the 18650 ran flat, the portal still answered and the console still showed `mode active portal`, but nothing moved. The stock hardware has no battery sensing, so the firmware can't tell.

## The TrimUI couldn't stay on the robot's network

**Symptom:** the TrimUI joined the robot's network, then dropped back to the home network within a few seconds. A phone stayed connected without trouble.

**What it wasn't:**
- **A crash:** the robot's console showed no reset.
- **The space in the old network name `Spider Robot`:** the network was renamed `mini-spooder` anyway, as the user wanted, and the TrimUI still dropped.
- **The TKIP group cipher:** the ESP8266's access point offers `WPA2-PSK-CCMP+TKIP`, and the SDK can't switch TKIP off. A probe pak (`handheld/tools/wifi-probe/`) showed the TrimUI completes the handshake (`wpa_state=COMPLETED`, group cipher TKIP) and stays connected for as long as nothing else intervenes.
- **The robot's DHCP server:** when the probe ran `udhcpc` itself, it got 192.168.4.101 within about 2 s.

**Something that confused the diagnosis:** Spooder's T-Display bridge was powered on the same desk, broadcasting `spooder-drive-6da9`. The TrimUI had that network saved and kept moving to it. Switching the bridge off removed that, but not the drop.

**The cause:** NextUI's `wifi_init.sh` starts one background `udhcpc -i wlan0 -b` when Wi-Fi comes up, and never restarts it. Joining another network in Settings changes only the `wpa_supplicant` association, so the TrimUI keeps its old home address, 192.168.0.84. With no usable address, NextUI moves back to a saved network within seconds. Spooder.pak worked because it runs its own `udhcpc` request when it starts.

**The fix:** MiniSpooder.pak now joins the robot itself. It selects the saved `mini-spooder` network, which disables the others for the session so NextUI can't move away. It then gets a fresh address with its own `udhcpc` run, and switches every network back on at exit. Afterwards it connected at once and drove the robot.

## The portal froze the robot

**Symptom:** the portal stopped answering, and in the `robot-trace` heartbeat one loop pass took **9,004,341 µs**. Ordinary page loads gave passes of 63–72 ms, against a 20 ms budget.

**Cause:** pages were sent with ESP8266WebServer's `send_P`, and each chunk waited up to 500 ms for room in the send buffer. When a client went away in the middle of a page, every one of about 18 chunks waited out its timeout. During such a pass nothing else ran: no motion ticks, and no drive link or failsafe.

**Fix:** a portal-owned sender writes only what the client's send buffer accepts in each pass, carries on in later passes, and drops a client that makes no progress for 2 s. With the fix, the longest pass while browsing and driving was about 7–9 ms, with none over 20 ms.

## Tools that helped

- **`robot-trace` build:** prints station joins and leaves and a 5 s heartbeat with the loop statistics (`pio run -d firmware -e robot-trace -t upload --upload-port "$ROBOT_PORT"`).
- **Console recording without resets:** opening the CP2102 port with DTR and RTS set low before `open()` avoids resetting the board, which matters when the state before an event is the evidence.
- **A phone over adb as a test station:** with no Wi-Fi on the development machine, an Android phone on the robot's network ran `ping` and `nc` against the portal through `adb shell`.
- **The TrimUI's own logs:** the SD card keeps NextUI's and the Settings pak's logs (`.userdata/tg5040/logs/`, `Tools/tg5040/Settings.pak/settings.log`), including `wpa_cli` scan results with each network's ciphers.
