/*
 * Project : EscapeRoom-HALLtoRelay - remote relay option (phase R1)
 * File    : RemoteLink.h - Master side of the ESP-NOW link to the Remote.
 *
 * Threads:
 *   - loop() (core 1) calls remoteSetTarget() and remoteTakeExpiry().
 *   - serverTask (core 0) calls remoteLinkService() (orders, retries,
 *     heartbeat, expiry) and the radio functions.
 *   - The ESP-NOW receive callback only copies the packet into a queue.
 *
 * Always compiled; the sketch calls these functions only when the
 * relay mode is remote (NVS key "rm", phase R2).
 */
#pragma once
#include <Arduino.h>
#include "Protocol.h"

enum LinkError : uint8_t {
  LINK_ERR_NONE = 0,
  LINK_ERR_ORDER,            // order not acknowledged after 5 failures
  LINK_ERR_EXPIRED_REMOTE,   // Remote answered "expired"
  LINK_ERR_EXPIRED_SILENCE   // no packet from the Remote for EXPIRY_MS
};

// Published by the service for the snapshot (RAM only)
struct LinkStatus {
  bool     online;           // packet from the Remote in the last 3 s
  bool     remoteRelayOn;    // relay state last reported by the Remote
  bool     orderConfirmed;   // current order acknowledged
  uint8_t  failures;         // failures of the current order
  bool     hasError;
  uint8_t  errorKind;        // LinkError
  uint32_t errorAtMs;        // time since boot
  uint32_t remoteBoot;
};

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>

static SemaphoreHandle_t linkMutex;
static QueueHandle_t     linkRxQueue;
static uint32_t          linkMasterBoot;
static bool              linkNetReady = false;

// Shared between loop() and the service (linkMutex)
static bool     shTargetOn = false;
static uint16_t shSeq = 0;
static bool     shExpireRequest = false;
static bool     shQuietValid = false;   // error kept until a newer order is confirmed
static uint16_t shQuietSeq = 0;
static LinkStatus shStatus = {};
volatile bool   linkOnlineFlag = false; // for the LED

// Service-local state (serverTask only)
static bool     svStarted = false;
static uint16_t svSeq = 0;
static bool     svTargetOn = false;
static bool     svConfirmed = false;
static uint8_t  svSends = 0;
static uint8_t  svFailures = 0;
static uint32_t svNextSendMs = 0, svLastSendMs = 0, svOrderStartMs = 0, svActiveSinceMs = 0;
static bool     svExpirePosted = false;
static bool     svEverRx = false;
static uint32_t svLastRxMs = 0;
static bool     svRemoteRelayOn = false;
static uint32_t svRemoteBoot = 0;
static bool     svHasError = false;
static uint8_t  svErrorKind = LINK_ERR_NONE;
static uint32_t svErrorAtMs = 0;
static bool     svWasOnline = false;
static uint32_t svLastPublishMs = 0;

// ---- ESP-NOW radio -------------------------------------------------------
static void linkOnRx(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  // Only copies into the queue (no logic, no NVS); foreign MACs are dropped here
  if (len <= 0 || len > (int)sizeof(RxItem::data)) return;
  if (memcmp(info->src_addr, REMOTE_MAC, 6) != 0) return;
  RxItem item;
  memcpy(item.mac, info->src_addr, 6);
  item.len = (uint8_t)len;
  memcpy(item.data, data, len);
  item.rxMs = millis();
  xQueueSend(linkRxQueue, &item, 0);
}

// Re-adds the Remote as a peer if a Wi-Fi mode change dropped it
static void ensurePeer() {
  if (!linkNetReady || esp_now_is_peer_exist(REMOTE_MAC)) return;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, REMOTE_MAC, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

// ESP-NOW and the AP share the radio: both must stay on ESPNOW_CHANNEL
static void enforceChannel() {
  uint8_t channel = 0;
  wifi_second_chan_t second;
  esp_wifi_get_channel(&channel, &second);
  if (channel != ESPNOW_CHANNEL) {
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  }
}

void remoteLinkInit() {
  linkMutex = xSemaphoreCreateMutex();
  linkRxQueue = xQueueCreate(8, sizeof(RxItem));
  linkMasterBoot = esp_random();
}

// Called once by serverTask: STA mode, fixed channel, ESP-NOW and the peer
void remoteRadioBegin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    return;
  }
  esp_now_register_recv_cb(linkOnRx);
  linkNetReady = true;
  ensurePeer();
  enforceChannel();
}

// Call after every AP on/off mode change
void remoteAfterModeChange() {
  enforceChannel();
  ensurePeer();
}

// ---- called by loop() -----------------------------------------------------
// Publishes the wanted relay state; a change creates a new order
void remoteSetTarget(bool on) {
  xSemaphoreTake(linkMutex, portMAX_DELAY);
  if (shTargetOn != on) {
    shTargetOn = on;
    shSeq++;
  }
  xSemaphoreGive(linkMutex);
}

// True when an active order expired: target goes back to rest (a new order)
// and the error stays visible until a newer order is confirmed.
bool remoteTakeExpiry() {
  bool expired = false;
  xSemaphoreTake(linkMutex, portMAX_DELAY);
  if (shExpireRequest) {
    shExpireRequest = false;
    if (shTargetOn) {
      shTargetOn = false;
      shSeq++;
      shQuietValid = true;
      shQuietSeq = shSeq;
      expired = true;
    }
  }
  xSemaphoreGive(linkMutex);
  return expired;
}

LinkStatus remoteLinkStatus() {
  LinkStatus s;
  xSemaphoreTake(linkMutex, portMAX_DELAY);
  s = shStatus;
  xSemaphoreGive(linkMutex);
  return s;
}

// ---- service (serverTask) -------------------------------------------------
static void registerError(uint8_t kind, uint32_t now) {
  svHasError = true;
  svErrorKind = kind;
  svErrorAtMs = now;
}

static void requestExpiry(uint8_t kind, uint32_t now) {
  if (svExpirePosted) return;
  svExpirePosted = true;
  xSemaphoreTake(linkMutex, portMAX_DELAY);
  if (shTargetOn && shSeq == svSeq) shExpireRequest = true;
  xSemaphoreGive(linkMutex);
  registerError(kind, now);
}

static void sendCmd() {
  if (!linkNetReady) return;
  CmdMsg msg = {MSG_CMD, PROTO_VERSION, linkMasterBoot, svSeq, (uint8_t)(svTargetOn ? 1 : 0)};
  esp_now_send(REMOTE_MAC, (const uint8_t*)&msg, sizeof(msg));
  svLastSendMs = millis();
}

static void handleRx(const RxItem& it) {
  if (it.len != sizeof(AckMsg)) return;
  AckMsg ack;
  memcpy(&ack, it.data, sizeof(ack));
  if (ack.type != MSG_ACK || ack.ver != PROTO_VERSION) return;

  svLastRxMs = it.rxMs;
  svEverRx = true;
  svRemoteRelayOn = ack.relayOn != 0;
  svRemoteBoot = ack.remoteBoot;

  if (ack.masterBoot != linkMasterBoot || ack.seq != svSeq) return;   // not the current order

  if (ack.status == ACK_EXPIRED) {
    if (svTargetOn) {
      requestExpiry(LINK_ERR_EXPIRED_REMOTE, it.rxMs);
    }
    return;
  }
  if (!svConfirmed) {
    svConfirmed = true;
    svFailures = 0;
    svNextSendMs = svLastSendMs + HEARTBEAT_MS;
    xSemaphoreTake(linkMutex, portMAX_DELAY);
    const bool keep = shQuietValid && !seqNewer(svSeq, shQuietSeq);
    xSemaphoreGive(linkMutex);
    if (!keep) svHasError = false;
  }
}

void remoteLinkService() {
  RxItem it;
  while (xQueueReceive(linkRxQueue, &it, 0) == pdTRUE) handleRx(it);
  const uint32_t now = millis();   // taken after the queue, so it is >= every rxMs

  bool on;
  uint16_t seq;
  xSemaphoreTake(linkMutex, portMAX_DELAY);
  on = shTargetOn;
  seq = shSeq;
  xSemaphoreGive(linkMutex);

  // New order
  if (!svStarted || seq != svSeq) {
    svStarted = true;
    svSeq = seq;
    svTargetOn = on;
    svConfirmed = false;
    svSends = 0;
    svFailures = 0;
    svNextSendMs = now;
    svOrderStartMs = now;
    svExpirePosted = false;
    if (on) svActiveSinceMs = now;
  }

  // Send, retry (unconfirmed) or heartbeat (confirmed)
  if ((int32_t)(now - svNextSendMs) >= 0) {
    if (!svConfirmed) {
      if (svSends > 0) {
        if (svFailures < 255) svFailures++;
        if (svFailures == ORDER_ERROR_FAILURES) registerError(LINK_ERR_ORDER, now);
      }
      sendCmd();
      const uint8_t idx = svSends < 4 ? svSends : 4;
      if (svSends < 255) svSends++;
      svNextSendMs = now + RETRY_MS[idx];
    } else {
      sendCmd();
      svNextSendMs = now + HEARTBEAT_MS;
    }
  }

  // An active order expires after EXPIRY_MS without packets from the Remote
  if (svTargetOn && !svExpirePosted) {
    const uint32_t ref = (svEverRx && (int32_t)(svLastRxMs - svActiveSinceMs) > 0) ? svLastRxMs : svActiveSinceMs;
    if ((now - ref) >= EXPIRY_MS) {
      requestExpiry(LINK_ERR_EXPIRED_SILENCE, now);
    }
  }

  const bool online = svEverRx && (now - svLastRxMs) < LINK_TIMEOUT_MS;
  linkOnlineFlag = online;
  if (online != svWasOnline) {
    svWasOnline = online;
  }

  if ((now - svLastPublishMs) >= 50) {
    svLastPublishMs = now;
    LinkStatus s = {online, svRemoteRelayOn, svConfirmed, svFailures,
                    svHasError, svErrorKind, svErrorAtMs, svRemoteBoot};
    xSemaphoreTake(linkMutex, portMAX_DELAY);
    shStatus = s;
    xSemaphoreGive(linkMutex);
  }
}
