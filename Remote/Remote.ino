/*
 * Project:     EscapeRoom-HALLtoRelay - remote relay option (phase R1)
 * Hardware:    M5Stack AtomS3 Lite + Relay Unit (Grove, DIN on GPIO 2)
 * Author:      <author>
 * Date:        <date>
 *
 * Description: The Remote. Receives relay orders from the Master over ESP-NOW
 *              (fixed MACs, channel 1), applies only newer orders, answers
 *              with an ACK and always boots with the relay at rest.
 *              If the relay is active and no packet arrives from the Master
 *              for EXPIRY_MS, the relay goes back to rest and that order is
 *              marked expired (an equal CMD does not reactivate it).
 *
 *              Relay: HIGH = active (NO and COM connected), LOW = rest.
 *              LED (brightness 24): green = relay at rest, blue = relay
 *              active; a red pulse every 2 s over that colour = no packet
 *              from the Master for more than 3 s.
 *              Button (GPIO 41): no function.
 *
 *              The ESP-NOW receive callback only copies packets to a queue;
 *              all logic runs in loop().
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_mac.h>
#include "Protocol.h"

constexpr uint8_t RELAY_PIN = 2;      // Grove DIN
constexpr uint8_t RELAY_ON  = HIGH;
constexpr uint8_t RELAY_OFF = LOW;
constexpr uint8_t BUTTON_PIN = 41;    // no function
constexpr uint8_t LED_PIN    = 35;
constexpr uint8_t LED_LEVEL  = 24;
constexpr uint32_t LED_PULSE_CYCLE_MS = 2000;
constexpr uint32_t LED_PULSE_MS       = 150;

QueueHandle_t rxQueue;
uint32_t remoteBoot;

bool relayOn = false;
bool haveOrder = false;
uint32_t curMasterBoot = 0;
uint16_t curSeq = 0;
bool orderExpired = false;
bool everRx = false;
uint32_t lastMasterRxMs = 0;

// ---------------------------------------------------------------------------
void setRelay(bool on) {
  digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
  relayOn = on;
}

void onRx(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  // Only copies into the queue; packets from other MACs are dropped here
  if (len <= 0 || len > (int)sizeof(RxItem::data)) return;
  if (memcmp(info->src_addr, MASTER_MAC, 6) != 0) return;
  RxItem item;
  memcpy(item.mac, info->src_addr, 6);
  item.len = (uint8_t)len;
  memcpy(item.data, data, len);
  item.rxMs = millis();
  xQueueSend(rxQueue, &item, 0);
}

void sendAck() {
  AckMsg ack = {MSG_ACK, PROTO_VERSION, curMasterBoot, curSeq, (uint8_t)(relayOn ? 1 : 0),
                (uint8_t)(orderExpired ? ACK_EXPIRED : ACK_OK), remoteBoot};
  esp_now_send(MASTER_MAC, (const uint8_t*)&ack, sizeof(ack));
}

void handleCmd(const RxItem& it) {
  if (it.len != sizeof(CmdMsg)) return;
  CmdMsg cmd;
  memcpy(&cmd, it.data, sizeof(cmd));
  if (cmd.type != MSG_CMD || cmd.ver != PROTO_VERSION) return;

  lastMasterRxMs = it.rxMs;
  everRx = true;

  const bool sameBoot = haveOrder && cmd.masterBoot == curMasterBoot;
  const bool isNew = !sameBoot || seqNewer(cmd.seq, curSeq);
  const bool isEqual = sameBoot && cmd.seq == curSeq;

  if (isNew) {
    haveOrder = true;
    curMasterBoot = cmd.masterBoot;
    curSeq = cmd.seq;
    orderExpired = false;
    setRelay(cmd.target != 0);
    sendAck();
  } else if (isEqual) {
    // Heartbeat or retry of the current order: answer, do not reapply
    sendAck();
  } else {
  }
}

// Active relay with no packet from the Master for EXPIRY_MS: back to rest
void checkExpiry(uint32_t now) {
  if (!relayOn) return;
  if ((now - lastMasterRxMs) >= EXPIRY_MS) {
    setRelay(false);
    orderExpired = true;
  }
}

// Colour = relay (green rest, blue active); red pulse = Master offline
void updateLed(uint32_t now) {
  static uint32_t lastColor = 0xFFFFFFFF;
  const bool masterOnline = everRx && (now - lastMasterRxMs) < LINK_TIMEOUT_MS;
  uint8_t r = 0, g = 0, b = 0;
  if (!masterOnline && (now % LED_PULSE_CYCLE_MS) < LED_PULSE_MS) r = LED_LEVEL;
  else if (relayOn) b = LED_LEVEL;
  else              g = LED_LEVEL;
  const uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  if (color == lastColor) return;
  lastColor = color;
  neopixelWrite(LED_PIN, r, g, b);
}

void ensureMasterPeer() {
  if (esp_now_is_peer_exist(MASTER_MAC)) return;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, MASTER_MAC, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

void setup() {
  // Relay to rest state before anything else
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);
  pinMode(BUTTON_PIN, INPUT);
  neopixelWrite(LED_PIN, 0, LED_LEVEL, 0);

  remoteBoot = esp_random();
  rxQueue = xQueueCreate(8, sizeof(RxItem));

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onRx);
    ensureMasterPeer();
  }
}

void loop() {
  RxItem it;
  while (xQueueReceive(rxQueue, &it, 0) == pdTRUE) handleCmd(it);
  const uint32_t now = millis();   // after the queue, so it is >= every rxMs
  checkExpiry(now);
  updateLed(now);
  delay(1);
}
