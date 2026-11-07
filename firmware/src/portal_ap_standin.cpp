#include "portal_ap_standin.h"

#include <ESP8266WiFi.h>

void portalStandInAccessPoint() {
  // drive-link's settings, applied from the firmware and never stored.
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  WiFi.softAP("Spider Robot", "87654321", 1, false, 4);
}
