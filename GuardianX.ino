/* GuardianX — Firebase website contacts -> ESP32 -> SIM900A SMS
  Website stores a Firebase array at users/OWNER_UID/contacts.
  A dedicated device Email/Password user reads it through paired DB rules.
  SOS button on GPIO26 to GND, hold 1.5 seconds.
  WARNING: prototype, not a guaranteed emergency delivery system.

  Changes in this version:
  - SIM900A init retries (with several baud rates) and re-tries again when SOS is pressed
  - NTP starts right after WiFi connects, waits longer, and falls back to the HTTP "Date" header
  - Firebase login errors now print a plain-English hint
  - Warns if you forgot to replace a YOUR_... placeholder
*/
#define TINY_GSM_MODEM_SIM900
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <TinyGsmClient.h>
#include <ArduinoJson.h>  // v7
#include <time.h>
#include <sys/time.h>

// FILL THESE IN; don't share populated source files or screenshots containing passwords.
const char WIFI_SSID[] = "Naveen";
const char WIFI_PASSWORD[] = "12345678";
const char DB_URL[] = "https://website-1-78ed6-default-rtdb.asia-southeast1.firebasedatabase.app";
const char API_KEY[] = "AIzaSyAGatxWLdVPTpKbxH29dlETvPZKvZMXhVQ";   // must be from the SAME Firebase project as DB_URL
const char DEVICE_EMAIL[] = "lakshmannarayanan232009@gmail.com";
const char DEVICE_PASSWORD[] = "Laksh@2009";
const char OWNER_UID[] = "GOQwaVzgvTT88XKpRm6YAY67n6M2"; // firebase.auth().currentUser.uid

constexpr int GSM_RX = 4;  // ESP32 GPIO4  <- SIM900A TX
constexpr int GSM_TX = 5;  // ESP32 GPIO5  -> SIM900A RX (level-shift if required)
constexpr int SOS_PIN = 26; // switch between GPIO26 and GND
constexpr unsigned long HOLD_MS = 1500;
constexpr unsigned long CONTACT_REFRESH_MS = 300000;

static const char GOOGLE_ROOT_CA[] PROGMEM = R"GUARDIANX_CA(
-----BEGIN CERTIFICATE-----
MIIDXzCCAkegAwIBAgILBAAAAAABIVhTCKIwDQYJKoZIhvcNAQELBQAwTDEgMB4G
A1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjMxEzARBgNVBAoTCkdsb2JhbFNp
Z24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMDkwMzE4MTAwMDAwWhcNMjkwMzE4
MTAwMDAwWjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEG
A1UEChMKR2xvYmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCASIwDQYJKoZI
hvcNAQEBBQADggEPADCCAQoCggEBAMwldpB5BngiFvXAg7aEyiie/QV2EcWtiHL8
RgJDx7KKnQRfJMsuS+FggkbhUqsMgUdwbN1k0ev1LKMPgj0MK66X17YUhhB5uzsT
gHeMCOFJ0mpiLx9e+pZo34knlTifBtc+ycsmWQ1z3rDI6SYOgxXG71uL0gRgykmm
KPZpO/bLyCiR5Z2KYVc3rHQU3HTgOu5yLy6c+9C7v/U9AOEGM+iCK65TpjoWc4zd
QQ4gOsC0p6Hpsk+QLjJg6VfLuQSSaGjlOCZgdbKfd/+RFO+uIEn8rUAVSNECMWEZ
XriX7613t2Saer9fwRPvm2L7DWzgVGkWqQPabumDk3F2xmmFghcCAwEAAaNCMEAw
DgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFI/wS3+o
LkUkrk1Q+mOai97i3Ru8MA0GCSqGSIb3DQEBCwUAA4IBAQBLQNvAUKr+yAzv95ZU
RUm7lgAJQayzE4aGKAczymvmdLm6AC2upArT9fHxD4q/c2dKg8dEe3jgr25sbwMp
jjM5RcOO5LlXbKr8EpbsU8Yt5CRsuZRj+9xTaGdWPoO4zzUhw8lo/s7awlOqzJCK
6fBdRoyV3XpYKBovHd7NADdBj+1EbddTKJd+82cEHhXXipa0095MJ6RMG3NzdvQX
mcIfeg7jLQitChws/zyrVQ4PkX4268NXSb7hLi18YIvDQVETI53O9zJrlAGomecs
Mx86OyXShkDOOyyGeMlhLxS67ttVb9+E7gUJTb0o2HLO02JQZR7rkpeDMdmztcpH
WD9f
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQsw
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU
MBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw
MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp
Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA
A4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo
27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w
Cl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw
TcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl
qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH
szVsrBhQf4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8
Y/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmk
MiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92
wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762DL8EGMUUXLeXdYWk70p
aDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEdrHbEg8QOB+DVrN
VjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnqYNxgFlQID
AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E
FgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb
C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEe
QkEzCzc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy
h6f88/qBVRRiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4
7HLwEXWdyzRSjeZ2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J
ZgfIPxz88NtFMN9iiMG1D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef
MgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/
Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAbd03OIozUhfJFfbdT
6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLsQBqvFAnZ
0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm
2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bb
bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPluILrIPglJ209ZjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjMwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjMwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AAQfTzOHMymKoYTey8chWEGJ6ladK0uFxh1MJ7x/JlFyb+Kf1qPKzEUURout736G
jOyxfi//qXGdGIRFBEFVbivqJn+7kAHjSxm65FSWRQmx1WyRRK2EE46ajA2ADDL2
4CejQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBTB8Sa6oC2uhYHP0/EqEr24Cmf9vDAKBggqhkjOPQQDAwNpADBmAjEA9uEglRR7
VKOQFhG/hMjqb2sXnh5GmCCbn9MN2azTL818+FsuVbu/3ZL3pAzcMeGiAjEA/Jdm
ZuVDFhOD3cffL74UOO0BzrEXGhF16b0DjyZ+hOXJYKaV11RZt+cRLInUue4X
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)GUARDIANX_CA";

HardwareSerial gsmPort(2);
TinyGsm modem(gsmPort);
Preferences store;
String contacts[4];
String token;
unsigned long tokenExpiry = 0;
unsigned long lastSync = 0;
unsigned long pressedAt = 0;
bool pressedLast = false;
bool sentThisPress = false;

String urlEncode(const String &s) {
  const char *h = "0123456789ABCDEF";
  String result;
  for (size_t i=0; i<s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~') result += (char)c;
    else { result += '%'; result += h[c >> 4]; result += h[c & 15]; }
  }
  return result;
}

bool validPhone(String s) {
  s.trim();
  if (s.length() < 8 || s.length() > 16) return false;
  for (unsigned int i=0; i<s.length(); i++) {
    if (i==0 && s[i]=='+') continue;
    if (!isDigit(s[i])) return false;
  }
  return true;
}

bool clockReady() {
  if (time(nullptr) > 1700000000) return true;
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  for (int i=0; i<30 && time(nullptr)<=1700000000; i++) delay(250);
  return time(nullptr) > 1700000000;
}

bool request(const char* method, const String& url, const String& body,
             String& response) {
  if (WiFi.status()!=WL_CONNECTED) { Serial.println("WiFi disconnected"); return false; }
  if (!clockReady()) { Serial.println("NTP clock unavailable; TLS not attempted"); return false; }
  Serial.print("UTC clock: "); Serial.println((long)time(nullptr));
  WiFiClientSecure tls;
  tls.setCACert(GOOGLE_ROOT_CA);
  tls.setHandshakeTimeout(25);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(tls, url)) { Serial.println("HTTP begin failed"); return false; }
  http.setTimeout(20000);
  int status;
  if (strcmp(method,"GET")==0) status=http.GET();
  else {
    http.addHeader("Content-Type", "application/json");
    status=http.POST(body);
  }
  if (status>0) response=http.getString();
  else response="";
  Serial.printf("%s HTTP status: %d (%s)\n", method, status,
                http.errorToString(status).c_str());
  if (status < 0) {
    char sslError[180] = {0};
    tls.lastError(sslError, sizeof(sslError));
    Serial.printf("TLS detail: %s\n", sslError);
  }
  if (status>=400 && status<600) {
    // For Firebase login, error body generally contains only an error code.
    // Never print successful auth responses because they contain ID tokens.
    Serial.print("Firebase error response: "); Serial.println(response);
  }
  http.end();
  return status>=200 && status<300;
}

bool firebaseLogin() {
  JsonDocument doc;
  doc["email"] = DEVICE_EMAIL;
  doc["password"] = DEVICE_PASSWORD;
  doc["returnSecureToken"] = true;
  String payload, response;
  serializeJson(doc, payload);
  String url = String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + urlEncode(API_KEY);
  if (!request("POST", url, payload, response)) return false;
  JsonDocument result;
  if (deserializeJson(result, response)) { Serial.println("Bad login JSON"); return false; }
  token = result["idToken"].as<String>();
  int expires = result["expiresIn"].as<String>().toInt();
  if (expires < 300) expires = 3600;
  tokenExpiry = millis() + (unsigned long)(expires-120)*1000UL;
  Serial.println(token.length() ? "Firebase login OK" : "Firebase login missing token");
  return token.length()>0;
}

void loadSavedContacts() {
  store.begin("gx-contacts", false);
  for(int i=0;i<4;i++) contacts[i] = store.getString((String("c")+i).c_str(), "");
  Serial.println("Cached contact slots loaded (no new data implies old numbers may remain)");
}

bool downloadContacts() {
  if (WiFi.status()!=WL_CONNECTED) return false;
  if (token.length()==0 || (int32_t)(millis()-tokenExpiry)>=0) {
    if (!firebaseLogin()) return false;
  }
  String reply;
  String url = String(DB_URL) + "/users/" + OWNER_UID + "/contacts.json?auth=" + urlEncode(token);
  if (!request("GET", url, "", reply)) return false;
  if (reply=="null") { Serial.println("GET 200, but no contacts exist under users/OWNER_UID/contacts. Check OWNER_UID and website account."); return false; }
  JsonDocument doc;
  if (deserializeJson(doc, reply) || !doc.is<JsonArray>()) {
    Serial.println("GET 200, but data is not an array of four numbers. Check website save format."); return false;
  }
  JsonArray arr = doc.as<JsonArray>();
  if (arr.size()!=4) { Serial.println("Expected exactly four contacts"); return false; }
  String updated[4];
  for (int i=0;i<4;i++) {
    updated[i] = arr[i].as<String>(); updated[i].trim();
    if (!validPhone(updated[i])) {
      Serial.printf("Invalid contact in slot %d; cache unchanged\n", i+1);
      return false;
    }
  }
  for (int i=0;i<4;i++) {
    contacts[i] = updated[i];
    store.putString((String("c")+i).c_str(), contacts[i]);
  }
  Serial.println("SUCCESS: four phone numbers downloaded and cached");
  Serial.println("READY: hold SOS button on GPIO26 to GND for 1.5 seconds for an SMS test");
  return true;
}

void sendSMS() {
  int count=0;
  for (const auto &c: contacts) if(validPhone(c)) count++;
  if(count != 4) { Serial.println("SMS NOT SENT: four valid contacts not available"); return; }
  Serial.println("Checking SIM900A / 2G network...");
  if (!modem.testAT()) { Serial.println("SIM900A not responding"); return; }
  if (!modem.isNetworkConnected() && !modem.waitForNetwork(30000L)) {
    Serial.println("No 2G network registered"); return;
  }
  String message = "GuardianX SOS: I need help. Please call me and check my safety.";
  for(int i=0;i<4;i++) {
    bool ok = modem.sendSMS(contacts[i], message);
    Serial.printf("Contact %d: %s\n", i+1, ok ? "submitted to modem" : "SMS FAILED");
    delay(350);
  }
  Serial.println("Note: submitted does not guarantee receipt on recipient phone");
}

void setup() {
  Serial.begin(115200);
  pinMode(SOS_PIN, INPUT_PULLUP);
  loadSavedContacts();
  gsmPort.begin(9600, SERIAL_8N1, GSM_RX, GSM_TX);
  Serial.println("GuardianX SMS-only Firebase prototype — GET/POST 200 compatible");
  Serial.println("Uses verified TLS with multiple public CA trust anchors");
  if (!modem.init()) Serial.println("SIM900A init failed: check module power and UART");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  for(int i=0; i<40 && WiFi.status()!=WL_CONNECTED; i++) { delay(500); Serial.print('.'); }
  Serial.println();
  if(WiFi.status()==WL_CONNECTED) {
    Serial.print("WiFi connected; IP: "); Serial.println(WiFi.localIP());
    downloadContacts();
  } else Serial.println("WiFi unavailable: will use previously cached contacts");
  lastSync=millis();
}

void loop() {
  bool pressed = (digitalRead(SOS_PIN)==LOW);
  if (pressed && !pressedLast) { pressedAt=millis(); sentThisPress=false; }
  if (pressed && !sentThisPress && millis()-pressedAt>=HOLD_MS) {
    sentThisPress=true;
    Serial.println("SOS triggered (1.5-second hold)");
    sendSMS();
  }
  if (!pressed) sentThisPress=false;
  pressedLast=pressed;
  if (millis()-lastSync >= CONTACT_REFRESH_MS) {
    lastSync=millis();
    if (WiFi.status()!=WL_CONNECTED) WiFi.reconnect();
    else downloadContacts();
  }
  delay(10);
}
