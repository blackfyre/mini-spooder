#!/bin/sh
# MiniSpooder WiFi probe: joins the robot's network (mini-spooder) for 25 s and
# logs the supplicant state, ciphers, link, address, a DHCP attempt and the
# kernel's messages, then re-enables every saved network. Nothing is saved to
# the supplicant's configuration. Log: /mnt/SDCARD/mini-spooder-wifi-probe.txt
OUT=/mnt/SDCARD/mini-spooder-wifi-probe.txt
SSID=mini-spooder
WPA="wpa_cli -p /etc/wifi/sockets -i wlan0"
SHOW=/mnt/SDCARD/.system/tg5040/bin/show2.elf
FIFO=/tmp/show2.fifo
LOGO=/mnt/SDCARD/.system/res/logo.png
SHOW_PID=

send() { [ -n "$SHOW_PID" ] && kill -0 "$SHOW_PID" 2>/dev/null && [ -p "$FIFO" ] && echo "$1" > "$FIFO"; }
say() { send "TEXT:$1"; echo "-- $(date +%T) $1" >> "$OUT"; }
section() { echo "== $(date +%T) $1" >> "$OUT"; }

restore() {
	$WPA enable_network all >> "$OUT" 2>&1
	$WPA reassociate >> "$OUT" 2>&1
	killall -9 show2.elf > /dev/null 2>&1
	rm -f "$FIFO"
	sync
}
trap restore EXIT INT TERM

echo "== MiniSpooder WiFi probe $(date)" > "$OUT"
killall -9 show2.elf > /dev/null 2>&1
rm -f "$FIFO"
"$SHOW" --mode=daemon --image="$LOGO" --logoheight=96 --fontsize=28 --text="MiniSpooder WiFi probe" > /dev/null 2>&1 &
SHOW_PID=$!
i=0
while [ ! -p "$FIFO" ] && [ $i -lt 5 ]; do sleep 1; i=$((i + 1)); done

section "before"
{ $WPA list_networks; $WPA status; iw dev wlan0 link; ip -4 addr show wlan0; uname -a; } >> "$OUT" 2>&1
section "dmesg before (tail)"
dmesg 2>&1 | tail -n 40 >> "$OUT"

ID=$($WPA list_networks | awk -F'\t' -v s="$SSID" '$2 == s {print $1; exit}')
if [ -z "$ID" ]; then
	say "No saved network $SSID: join it once in NextUI's WiFi settings"
	sleep 8
	exit 0
fi
section "network $ID settings"
for k in ssid key_mgmt proto pairwise group scan_ssid ieee80211w; do echo "$k=$($WPA get_network "$ID" "$k")" >> "$OUT"; done

say "Joining $SSID (network $ID)"
$WPA select_network "$ID" >> "$OUT" 2>&1
n=0
while [ $n -lt 25 ]; do
	section "t+$n s"
	$WPA status 2>&1 | grep -E "^(wpa_state|bssid|ssid|pairwise_cipher|group_cipher|key_mgmt|ip_address|freq)=" >> "$OUT"
	iw dev wlan0 link 2>&1 | grep -E "Connected|signal|tx bitrate|rx bitrate|Not connected" >> "$OUT"
	ip -4 addr show wlan0 2>&1 | awk '/inet /{print "inet " $2}' >> "$OUT"
	send "PROGRESS:$((n * 4))"
	[ $n -eq 10 ] && { section "udhcpc attempt"; udhcpc -i wlan0 -n -q -t 5 >> "$OUT" 2>&1; echo "udhcpc exit $?" >> "$OUT"; }
	sleep 1
	n=$((n + 1))
done
section "ping 192.168.4.1"
ping -c 3 -W 1 192.168.4.1 >> "$OUT" 2>&1
section "dmesg after (tail)"
dmesg 2>&1 | tail -n 80 >> "$OUT"
[ -x /sbin/logread ] && { section "logread (tail)"; logread 2>&1 | tail -n 80 >> "$OUT"; }
say "Done: log in mini-spooder-wifi-probe.txt"
sleep 4
