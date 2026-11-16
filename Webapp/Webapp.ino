/*
 * Project:     EscapeRoom-HALLtoRelay
 * Phase:       R2 - relay mode (local / remote over ESP-NOW) chosen in the webapp
 * Hardware:    M5Stack AtomS3 Lite + Atomic Port ABC Base + 4 x HALL Unit
 *              + Relay Unit (via Unit Hub on the Grove port)
 * Author:      <author>
 * Date:        <date>
 *
 * Description: Game from phase 1 (latched relay opened when sensors 1..N all
 *              keep the opening combination for the stability time, released
 *              when the rearm combination is kept for the stability time)
 *              plus a configuration mode: a 3 s long press of the button
 *              switches the Wi-Fi access point and the webapp on or off.
 *
 *              Threads:
 *                - loop() (core 1): game, button, LED. Only this code writes
 *                  the settings, the game state and the relay.
 *                - serverTask (core 0): access point and web server. It never
 *                  touches the game; it reads a snapshot and leaves requests
 *                  that loop() applies.
 *
 *              LED: green = armed, blue = waiting rearm; white pulses while the
 *              AP is on (1 long = no client, 2 short = client connected);
 *              3 fast white blinks = long press recognised.
 *
 *              A sensor is "active" (ON) when a magnet is detected (pin LOW).
 *              See SPEC.md.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <WebServer.h>
#include "WebPage.h"

#include "RemoteLink.h"

// Relay installation, read from the NVS (key "rm") once at boot and never changed
// while running: false = local Relay Unit on the Master (factory), true = remote
// relay over ESP-NOW. Changing it saves the key and reboots.
bool relayRemote = false;

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------
constexpr uint8_t MAX_SENSORS = 4;
constexpr uint8_t SENSOR_PINS[MAX_SENSORS] = {39, 8, 6, 1};  // Port A, B, C, Grove white
constexpr uint8_t RELAY_PIN = 2;                             // Grove yellow (DIN)
constexpr uint8_t RELAY_ON  = HIGH;                          // NO and COM connected
constexpr uint8_t RELAY_OFF = LOW;                           // rest state
constexpr uint8_t BUTTON_PIN = 41;                           // AtomS3 Lite button (M5Unified)
constexpr uint8_t LED_PIN    = 35;                           // AtomS3 Lite WS2812 (M5Unified)

// ---------------------------------------------------------------------------
// Settings (factory values and limits from SPEC.md)
// ---------------------------------------------------------------------------
constexpr uint8_t  FACTORY_SENSOR_COUNT = 4;
constexpr bool     FACTORY_INVERT       = false;
constexpr uint32_t FACTORY_STABILITY_MS = 3000;
constexpr uint32_t MIN_STABILITY_MS     = 500;
constexpr uint32_t MAX_STABILITY_MS     = 60000;

// ---------------------------------------------------------------------------
// Access point, button and LED
// ---------------------------------------------------------------------------
constexpr char AP_SSID_PREFIX[] = "RSwitchE-Lock";
constexpr char AP_PASSWORD[]    = "mission2escape";
char apSsid[20];   // "RSwitchE-Lock-XXXX", built at boot from the SoftAP MAC

constexpr uint32_t LONG_PRESS_MS      = 3000;
constexpr uint32_t BUTTON_RELEASE_MS  = 50;     // debounce for the release
constexpr uint32_t SNAPSHOT_PERIOD_MS = 50;

// LED pattern timing (same approach as the EscapeRoom audio device)
constexpr uint32_t LED_CONFIRM_BLINK_MS = 100;   // long press: 3 fast white blinks
constexpr uint8_t  LED_CONFIRM_BLINKS   = 3;
constexpr uint32_t LED_AP_CYCLE_MS      = 3000;  // AP on: white pulses over the colour
constexpr uint32_t LED_AP_LONG_MS       = 1000;  // no client: 1 long pulse
constexpr uint32_t LED_AP_SHORT_MS      = 250;   // with a client: 2 short pulses
constexpr uint32_t LED_AP_SHORT_GAP_MS  = 500;   // start of the second short pulse

constexpr uint8_t LED_LEVEL = 24;                // low brightness (0..255)
constexpr uint32_t LED_LINK_CYCLE_MS = 2000;     // remote mode: red pulse while the link is down
constexpr uint32_t LED_LINK_PULSE_MS = 150;

struct Settings {
  uint8_t  sensorCount;
  bool     invert;
  uint32_t stabilityMs;
};

Settings settings;           // owned by loop()
Preferences prefs;

// ---------------------------------------------------------------------------
// Game state (owned by loop())
// ---------------------------------------------------------------------------
enum class GameState : uint8_t { Armed, WaitingRearm };

GameState gameState = GameState::Armed;
bool comboHeld = false;           // target combination currently being counted
unsigned long comboStartMs = 0;   // when the current count started

// ---------------------------------------------------------------------------
// Data shared with the server task (protected by sharedMutex)
// ---------------------------------------------------------------------------
struct Snapshot {
  Settings  settings;
  GameState state;
  bool      relayOn;
  bool      held;
  uint32_t  elapsedMs;
  bool      sensors[MAX_SENSORS];   // true = magnet detected (ON)
  LinkStatus link;                  // remote relay link (zeros in local mode)
};

SemaphoreHandle_t sharedMutex;
Snapshot snapshot;
Settings pendingSettings;
bool pendingSettingsFlag = false;
bool pendingOpenFlag = false;
bool pendingModeFlag = false;
bool pendingModeRemote = false;
bool restartPending = false;          // loop() only
unsigned long restartAtMs = 0;        // loop() only

volatile bool apWanted = false;   // set by the button, applied by serverTask
volatile uint8_t apClients = 0;   // stations on the AP, updated by serverTask

WebServer server(80);

// ---------------------------------------------------------------------------
// Settings persistence
// ---------------------------------------------------------------------------
bool settingsValid(const Settings& s) {
  return s.sensorCount >= 1 && s.sensorCount <= MAX_SENSORS &&
         s.stabilityMs >= MIN_STABILITY_MS && s.stabilityMs <= MAX_STABILITY_MS;
}

void saveSettings(const Settings& s) {
  prefs.begin("hallrelay", false);
  // Write only what changed, to spare the flash
  if (prefs.getUChar("n", 0xFF) != s.sensorCount)       prefs.putUChar("n", s.sensorCount);
  if (prefs.getBool("inv", !s.invert) != s.invert)      prefs.putBool("inv", s.invert);
  if (prefs.getULong("stab_ms", 0) != s.stabilityMs)    prefs.putULong("stab_ms", s.stabilityMs);
  prefs.end();
}

void loadSettings() {
  Settings loaded;
  prefs.begin("hallrelay", true);
  loaded.sensorCount = prefs.getUChar("n", FACTORY_SENSOR_COUNT);
  loaded.invert      = prefs.getBool("inv", FACTORY_INVERT);
  loaded.stabilityMs = prefs.getULong("stab_ms", FACTORY_STABILITY_MS);
  prefs.end();

  if (!settingsValid(loaded)) {
    loaded = {FACTORY_SENSOR_COUNT, FACTORY_INVERT, FACTORY_STABILITY_MS};
  }
  settings = loaded;
}

// Relay installation mode (not part of the game settings: the factory reset
// does not touch it). Absent or invalid = local.
void loadRelayMode() {
  prefs.begin("hallrelay", true);
  relayRemote = prefs.getUChar("rm", 0) == 1;
  prefs.end();
}

void saveRelayMode(bool remote) {
  prefs.begin("hallrelay", false);
  if (prefs.getUChar("rm", 0xFF) != (remote ? 1 : 0)) prefs.putUChar("rm", remote ? 1 : 0);
  prefs.end();
}

// ---------------------------------------------------------------------------
// Sensors
// ---------------------------------------------------------------------------
void initSensors() {
  // All four pins, so a higher sensor count from the webapp needs no reboot
  for (uint8_t i = 0; i < MAX_SENSORS; i++) {
    pinMode(SENSOR_PINS[i], INPUT);  // as in the official M5Stack HALL example
  }
}

bool magnetDetected(uint8_t index) {
  return digitalRead(SENSOR_PINS[index]) == LOW;
}

// True when every sensor in use matches the wanted magnet state
bool allSensorsMagnet(bool wantMagnet) {
  for (uint8_t i = 0; i < settings.sensorCount; i++) {
    if (magnetDetected(i) != wantMagnet) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Relay
// ---------------------------------------------------------------------------
bool relayActive = false;

void setRelay(bool on) {
  if (relayRemote) {
    remoteSetTarget(on);   // the serverTask delivers it to the Remote
  } else {
    digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
  }
  relayActive = on;
}

// ---------------------------------------------------------------------------
// Game logic
// ---------------------------------------------------------------------------
// Counts how long the target combination is held. Returns true once it has
// been held for the stability time; any change restarts the count.
bool combinationStable(bool comboNow) {
  const unsigned long now = millis();
  if (!comboNow) {
    comboHeld = false;
    return false;
  }
  if (!comboHeld) {
    comboHeld = true;
    comboStartMs = now;
  }
  return (now - comboStartMs) >= settings.stabilityMs;
}

void enterWaitingRearm() {
  setRelay(true);
  gameState = GameState::WaitingRearm;
  comboHeld = false;
}

void updateGame() {
  // Without inversion: opening = no magnet, rearm = magnet
  if (gameState == GameState::Armed) {
    const bool openCombo = allSensorsMagnet(settings.invert);
    if (combinationStable(openCombo)) {
      enterWaitingRearm();
    }
  } else {
    const bool rearmCombo = allSensorsMagnet(!settings.invert);
    if (combinationStable(rearmCombo)) {
      setRelay(false);
      gameState = GameState::Armed;
      comboHeld = false;
    }
  }
}

// ---------------------------------------------------------------------------
// Requests from the webapp, applied by loop()
// ---------------------------------------------------------------------------
void applyPendingRequests() {
  Settings newSettings;
  bool applySettings = false;
  bool applyOpen = false;
  bool applyMode = false;
  bool newModeRemote = false;

  xSemaphoreTake(sharedMutex, portMAX_DELAY);
  if (pendingModeFlag) {
    applyMode = true;
    newModeRemote = pendingModeRemote;
    pendingModeFlag = false;
  }
  if (pendingSettingsFlag) {
    newSettings = pendingSettings;
    applySettings = true;
    pendingSettingsFlag = false;
  }
  if (pendingOpenFlag) {
    applyOpen = true;
    pendingOpenFlag = false;
  }
  xSemaphoreGive(sharedMutex);

  if (applySettings && settingsValid(newSettings)) {
    settings = newSettings;
    comboHeld = false;   // restart any count; state and relay are kept
  }
  if (applyMode && !restartPending && gameState == GameState::Armed &&
      newModeRemote != relayRemote) {
    saveRelayMode(newModeRemote);
    restartPending = true;
    restartAtMs = millis() + 800;   // lets the HTTP answer leave first
  }
  if (applyOpen && !restartPending && gameState == GameState::Armed) {
    enterWaitingRearm();
  }
}

// Applies an expiry of the active order decided by the link service: the relay
// target goes back to rest, the game stays in "waiting rearm" (it does not reopen)
void applyLinkEvents() {
  if (relayRemote && remoteTakeExpiry()) {
    relayActive = false;
  }
}

void updateSnapshot() {
  Snapshot s;
  s.settings = settings;
  s.state = gameState;
  s.relayOn = relayActive;
  s.held = comboHeld;
  s.elapsedMs = comboHeld ? (uint32_t)(millis() - comboStartMs) : 0;
  for (uint8_t i = 0; i < MAX_SENSORS; i++) {
    s.sensors[i] = magnetDetected(i);
  }
  if (relayRemote) s.link = remoteLinkStatus();
  else             s.link = {};
  xSemaphoreTake(sharedMutex, portMAX_DELAY);
  snapshot = s;
  xSemaphoreGive(sharedMutex);
}

// ---------------------------------------------------------------------------
// Button (long press toggles the access point) and LED
// ---------------------------------------------------------------------------
unsigned long ledConfirmStartMs = 0;
bool ledConfirmActive = false;

void updateButton() {
  static bool pressed = false;
  static bool fired = false;
  static unsigned long pressStartMs = 0;
  static unsigned long releaseSinceMs = 0;

  const unsigned long now = millis();
  const bool down = digitalRead(BUTTON_PIN) == LOW;

  if (down) {
    releaseSinceMs = 0;
    if (!pressed) {
      pressed = true;
      fired = false;
      pressStartMs = now;
    }
    if (!fired && (now - pressStartMs) >= LONG_PRESS_MS) {
      fired = true;
      apWanted = !apWanted;
      ledConfirmStartMs = now;
      ledConfirmActive = true;
    }
  } else if (pressed) {
    if (releaseSinceMs == 0) releaseSinceMs = now;
    if ((now - releaseSinceMs) >= BUTTON_RELEASE_MS) {
      pressed = false;   // a new long press is only accepted after the release
    }
  }
}

void writeLed(uint8_t r, uint8_t g, uint8_t b) {
  static uint32_t lastColor = 0xFFFFFFFF;
  const uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  if (color == lastColor) return;
  lastColor = color;
  neopixelWrite(LED_PIN, r, g, b);
}

// True while the AP white pulse is on: 1 long pulse per cycle without clients,
// 2 short pulses per cycle with at least one client.
bool apPulseOn(unsigned long now) {
  const uint32_t phase = now % LED_AP_CYCLE_MS;
  if (apClients == 0) return phase < LED_AP_LONG_MS;
  return phase < LED_AP_SHORT_MS ||
         (phase >= LED_AP_SHORT_GAP_MS && phase < LED_AP_SHORT_GAP_MS + LED_AP_SHORT_MS);
}

// Colour = game state (green armed, blue waiting rearm). White overrides it:
// 3 fast blinks on a long press, pulses while the AP is on.
void updateLed() {
  const unsigned long now = millis();
  if (ledConfirmActive) {
    const unsigned long elapsed = now - ledConfirmStartMs;
    if (elapsed < 2UL * LED_CONFIRM_BLINKS * LED_CONFIRM_BLINK_MS) {
      if ((elapsed / LED_CONFIRM_BLINK_MS) % 2 == 0) writeLed(LED_LEVEL, LED_LEVEL, LED_LEVEL);
      else                                           writeLed(0, 0, 0);
      return;
    }
    ledConfirmActive = false;
  }
  if (apWanted && apPulseOn(now)) {
    writeLed(LED_LEVEL, LED_LEVEL, LED_LEVEL);
    return;
  }
  // Remote mode only: short red pulse every 2 s while the Remote link is down
  if (relayRemote && !linkOnlineFlag && (now % LED_LINK_CYCLE_MS) < LED_LINK_PULSE_MS) {
    writeLed(LED_LEVEL, 0, 0);
    return;
  }
  if (gameState == GameState::Armed) writeLed(0, LED_LEVEL, 0);
  else                               writeLed(0, 0, LED_LEVEL);
}

// ---------------------------------------------------------------------------
// Web server handlers (run in serverTask, core 0)
// ---------------------------------------------------------------------------
Snapshot readSnapshot() {
  Snapshot s;
  xSemaphoreTake(sharedMutex, portMAX_DELAY);
  s = snapshot;
  xSemaphoreGive(sharedMutex);
  return s;
}

void sendJson(int code, const char* body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

bool parseUnsigned(const String& text, uint32_t& value) {
  if (text.length() == 0 || text.length() > 9) return false;
  for (size_t i = 0; i < text.length(); i++) {
    if (!isDigit(text[i])) return false;
  }
  value = (uint32_t)text.toInt();
  return true;
}

void handleRoot() {
  server.send_P(200, "text/html", WEB_PAGE);
}

// Slow data: settings, limits, access point and uptime
void handleStatus() {
  const Snapshot s = readSnapshot();
  char body[340];
  snprintf(body, sizeof(body),
           "{\"p\":{\"n\":%u,\"inv\":%u,\"stab\":%lu},"
           "\"lim\":{\"nMin\":1,\"nMax\":%u,\"stabMin\":%lu,\"stabMax\":%lu},"
           "\"ap\":{\"clients\":%u,\"ssid\":\"%s\"},\"rm\":%u,\"st\":%u,\"up\":%lu}",
           s.settings.sensorCount, s.settings.invert ? 1 : 0,
           (unsigned long)s.settings.stabilityMs,
           MAX_SENSORS, (unsigned long)MIN_STABILITY_MS, (unsigned long)MAX_STABILITY_MS,
           (unsigned)apClients, apSsid, relayRemote ? 1 : 0, s.state == GameState::Armed ? 0 : 1,
           (unsigned long)millis());
  sendJson(200, body);
}

// Fast data: sensors, game state, relay and the running count
void handleLive() {
  const Snapshot s = readSnapshot();
  char body[420];
  // Remote link (all zeros in local mode): online, remote relay, order confirmed,
  // failures, error kind (0 = none), error age in ms, remote boot id
  const uint32_t errAgeMs = s.link.hasError ? (uint32_t)(millis() - s.link.errorAtMs) : 0;
  snprintf(body, sizeof(body),
           "{\"n\":%u,\"s\":[%u,%u,%u,%u],\"state\":%u,\"relay\":%u,"
           "\"held\":%u,\"elapsed\":%lu,\"stab\":%lu,"
           "\"link\":{\"on\":%u,\"rr\":%u,\"ok\":%u,\"f\":%u,\"e\":%u,\"ea\":%lu,\"rb\":\"%08lX\"}}",
           s.settings.sensorCount,
           s.sensors[0], s.sensors[1], s.sensors[2], s.sensors[3],
           s.state == GameState::Armed ? 0 : 1, s.relayOn ? 1 : 0,
           s.held ? 1 : 0, (unsigned long)s.elapsedMs,
           (unsigned long)s.settings.stabilityMs,
           s.link.online ? 1 : 0, s.link.remoteRelayOn ? 1 : 0, s.link.orderConfirmed ? 1 : 0,
           s.link.failures, s.link.hasError ? s.link.errorKind : 0, (unsigned long)errAgeMs,
           (unsigned long)s.link.remoteBoot);
  sendJson(200, body);
}

// Saves to NVS and leaves the new settings for loop() to apply
void commitSettings(const Settings& s) {
  saveSettings(s);
  xSemaphoreTake(sharedMutex, portMAX_DELAY);
  pendingSettings = s;
  pendingSettingsFlag = true;
  xSemaphoreGive(sharedMutex);
}

void handleSettings() {
  uint32_t n = 0, inv = 0, stab = 0;
  if (!parseUnsigned(server.arg("n"), n) ||
      !parseUnsigned(server.arg("inv"), inv) ||
      !parseUnsigned(server.arg("stab"), stab) || inv > 1) {
    sendJson(400, "{\"ok\":false,\"error\":\"Valores inválidos.\"}");
    return;
  }
  const Settings s = {(uint8_t)(n > 255 ? 255 : n), inv == 1, stab};
  if (!settingsValid(s)) {
    sendJson(400, "{\"ok\":false,\"error\":\"Valores fora dos limites.\"}");
    return;
  }
  commitSettings(s);
  sendJson(200, "{\"ok\":true}");
}

void handleFactory() {
  commitSettings({FACTORY_SENSOR_COUNT, FACTORY_INVERT, FACTORY_STABILITY_MS});
  sendJson(200, "{\"ok\":true}");
}

void handleOpen() {
  if (readSnapshot().state != GameState::Armed) {
    sendJson(409, "{\"ok\":false,\"error\":\"O jogo já está a aguardar rearme.\"}");
    return;
  }
  xSemaphoreTake(sharedMutex, portMAX_DELAY);
  pendingOpenFlag = true;
  xSemaphoreGive(sharedMutex);
  sendJson(200, "{\"ok\":true}");
}

// Relay installation: only with the game Armed; loop() saves the key and reboots
void handleRelayMode() {
  uint32_t rm = 0;
  if (!parseUnsigned(server.arg("rm"), rm) || rm > 1) {
    sendJson(400, "{\"ok\":false,\"error\":\"Valor inválido.\"}");
    return;
  }
  if ((rm == 1) == relayRemote) {
    sendJson(200, "{\"ok\":true,\"changed\":false}");
    return;
  }
  if (readSnapshot().state != GameState::Armed) {
    sendJson(409, "{\"ok\":false,\"error\":\"Só é possível mudar com o jogo Armado.\"}");
    return;
  }
  xSemaphoreTake(sharedMutex, portMAX_DELAY);
  pendingModeRemote = rm == 1;
  pendingModeFlag = true;
  xSemaphoreGive(sharedMutex);
  sendJson(200, "{\"ok\":true,\"changed\":true}");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void startAccessPoint() {
  WiFi.persistent(false);
  if (relayRemote) {
    WiFi.mode(WIFI_AP_STA);                      // STA side keeps ESP-NOW alive
    WiFi.softAPsetHostname(apSsid);
    WiFi.softAP(apSsid, AP_PASSWORD, ESPNOW_CHANNEL);
    remoteAfterModeChange();
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAPsetHostname(apSsid);
    WiFi.softAP(apSsid, AP_PASSWORD);
  }
  server.begin();
}

void stopAccessPoint() {
  server.stop();
  WiFi.softAPdisconnect(true);
  if (relayRemote) {
    WiFi.mode(WIFI_STA);
    remoteAfterModeChange();
  } else {
    WiFi.mode(WIFI_OFF);
  }
}

// Owns the access point and the web server; never touches the game directly
void serverTask(void*) {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/live", HTTP_GET, handleLive);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/factory", HTTP_POST, handleFactory);
  server.on("/api/open", HTTP_POST, handleOpen);
  server.on("/api/relaymode", HTTP_POST, handleRelayMode);
  server.onNotFound(handleNotFound);

  if (relayRemote) remoteRadioBegin();   // local mode: no ESP-NOW, no heartbeat
  bool apActive = false;
  for (;;) {
    if (relayRemote) remoteLinkService();
    if (apWanted && !apActive) {
      startAccessPoint();
      apActive = true;
    } else if (!apWanted && apActive) {
      stopAccessPoint();
      apActive = false;
    }
    if (apActive) {
      server.handleClient();
      apClients = WiFi.softAPgetStationNum();
    } else {
      apClients = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
// Last two bytes of the SoftAP MAC, read from eFuse (works before Wi-Fi starts)
void buildApSsid() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(apSsid, sizeof(apSsid), "%s-%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
}

void setup() {
  loadRelayMode();
  if (relayRemote) {
    remoteLinkInit();   // the relay is in the Remote; GPIO 2 is left unused
  } else {
    // Relay to rest state before anything else
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, RELAY_OFF);
  }
  pinMode(BUTTON_PIN, INPUT);   // as in M5Unified for the AtomS3 Lite
  buildApSsid();
  loadSettings();
  initSensors();

  sharedMutex = xSemaphoreCreateMutex();
  updateSnapshot();
  writeLed(0, LED_LEVEL, 0);    // armed, AP off

  xTaskCreatePinnedToCore(serverTask, "server", 8192, nullptr, 1, nullptr, 0);

}

void loop() {
  static unsigned long lastSnapshotMs = 0;

  applyLinkEvents();
  applyPendingRequests();
  updateGame();
  updateButton();
  updateLed();

  const unsigned long now = millis();
  if (restartPending && (long)(now - restartAtMs) >= 0) {
    ESP.restart();   // applies the new relay mode; boots Armed with the relay at rest
  }
  if (now - lastSnapshotMs >= SNAPSHOT_PERIOD_MS) {
    lastSnapshotMs = now;
    updateSnapshot();
  }
}
