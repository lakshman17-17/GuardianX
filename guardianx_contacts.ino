// GuardianX: receive 4 emergency numbers from the website and keep them in flash.
// Website sends them from the database either as a serial line  "CONTACTS:n1,n2,n3,n4\n"
// or as a Wi-Fi request  GET /contacts?n=n1,n2,n3,n4
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "config.h"          // Wi-Fi, Firebase URL, device id


Preferences prefs;
WebServer server(80);
String contacts[4];

void saveContacts(String csv) {                       // "n1,n2,n3,n4"
  prefs.begin("gx", false);
  for (int i = 0; i < 4; i++) {
    int c = csv.indexOf(',');
    contacts[i] = (c < 0) ? csv : csv.substring(0, c);
    csv = (c < 0) ? "" : csv.substring(c + 1);
    prefs.putString(("c" + String(i)).c_str(), contacts[i]);
  }
  prefs.end();
}

void loadContacts() {                                 // survives reboot
  prefs.begin("gx", true);
  for (int i = 0; i < 4; i++) contacts[i] = prefs.getString(("c" + String(i)).c_str(), "");
  prefs.end();
}

void pullFromFirebase() {                             // reads the 4 numbers saved by the website
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();                               // demo only; pin the certificate for production
  HTTPClient http;
  if (!http.begin(client, FB_URL + "/contacts.json")) return;
  if (http.GET() == 200) {
    String body = http.getString(), csv = "";         // ["+91...","+91...","+91...","+91..."]
    int pos = 0, n = 0;
    while (n < 4) {
      int a = body.indexOf('"', pos);
      if (a < 0) break;
      int b = body.indexOf('"', a + 1);
      if (b < 0) break;
      csv += (n ? "," : "") + body.substring(a + 1, b);
      pos = b + 1;
      n++;
    }
    if (n == 4) saveContacts(csv);
  }
  http.end();
}

String fb(const String& path, const char* method, const String& body = "") {   // one Firebase REST call
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String out = "";
  if (WiFi.status() == WL_CONNECTED && http.begin(client, FB_URL + path + ".json")) {
    if (http.sendRequest(method, body) == 200) out = http.getString();
    http.end();
  }
  return out;
}

// Call from your GPS loop about every 3 s. The website listens to this node and moves the map marker.
void pushLive(double lat, double lng, int bat, int gsm, bool sos) {
  String j = "{\"lat\":" + String(lat, 6) + ",\"lng\":" + String(lng, 6) + ",\"bat\":" + bat +
             ",\"gsm\":" + gsm + ",\"sos\":" + (sos ? 1 : 0) + ",\"t\":" + millis() + "}";
  fb("/devices/" + DEVICE_ID + "/live", "PUT", j);
}

// Call about every 2 s. True once after the website's Respond button was pressed (then it is cleared).
bool checkRespond() {
  String p = "/devices/" + DEVICE_ID + "/command/respond";
  if (fb(p, "GET") != "true") return false;
  fb(p, "PUT", "false");
  return true;                                        // -> vibration ON, LED OFF
}

void handleContacts() {
  saveContacts(server.arg("n"));
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  loadContacts();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  server.on("/contacts", handleContacts);
  server.begin();
}

void loop() {
  server.handleClient();
  static unsigned long t = 0;                         // refresh from Firebase at boot, then every 60 s
  if (WiFi.status() == WL_CONNECTED && (t == 0 || millis() - t > 60000)) { pullFromFirebase(); t = millis(); }
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s.startsWith("CONTACTS:")) saveContacts(s.substring(9));
  }
}

// On SOS: for each non-empty contacts[i], send the SMS with the SIM900A:
//   AT+CMGF=1  ->  AT+CMGS="<number>"  ->  message text + Ctrl+Z (0x1A)
