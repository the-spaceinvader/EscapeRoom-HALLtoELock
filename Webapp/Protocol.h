/*
 * Project : EscapeRoom-HALLtoRelay - remote relay option (phase R1)
 * File    : Protocol.h - ESP-NOW protocol shared by the Master and the Remote.
 *
 * IMPORTANT: Webapp/Protocol.h and Remote/Protocol.h must be IDENTICAL
 * (check with diff). Do not move it to a shared folder: arduino-cli copies
 * only the sketch folder.
 *
 * Model: the Master sends the wanted relay state ("order") identified by
 * (masterBoot, seq). The Remote applies only newer orders and answers with an
 * ACK. A heartbeat is the current CMD resent without a new seq.
 */
#pragma once
#include <stdint.h>

constexpr uint8_t PROTO_VERSION  = 1;
constexpr uint8_t ESPNOW_CHANNEL = 1;   // shared by the AP and ESP-NOW

// Factory STA MACs, fixed in the firmware (no pairing). If a board is
// replaced, update these two lines and reload BOTH boards.
constexpr uint8_t MASTER_MAC[6] = {0xE8, 0xF6, 0x0A, 0x98, 0x5B, 0x4C};
constexpr uint8_t REMOTE_MAC[6] = {0x44, 0x1B, 0xF6, 0xF6, 0x6E, 0xF0};

// Timing
constexpr uint32_t HEARTBEAT_MS       = 1000;   // current CMD resent every 1 s
constexpr uint32_t LINK_TIMEOUT_MS    = 3000;   // offline after 3 s without packets
constexpr uint32_t RETRY_MS[5]        = {250, 500, 1000, 2000, 2000};  // then 2000 forever
constexpr uint8_t  ORDER_ERROR_FAILURES = 5;    // order error flagged at the 5th failure

constexpr uint32_t EXPIRY_MS = 60000;           // lost-link expiry of an active order

enum MsgType : uint8_t { MSG_CMD = 1, MSG_ACK = 2 };
enum AckStatus : uint8_t { ACK_OK = 0, ACK_EXPIRED = 1 };

struct __attribute__((packed)) CmdMsg {
  uint8_t  type;        // MSG_CMD
  uint8_t  ver;
  uint32_t masterBoot;  // random at every Master boot
  uint16_t seq;         // grows with every new order
  uint8_t  target;      // 1 = relay active, 0 = rest
};

struct __attribute__((packed)) AckMsg {
  uint8_t  type;        // MSG_ACK
  uint8_t  ver;
  uint32_t masterBoot;
  uint16_t seq;
  uint8_t  relayOn;     // real relay state in the Remote
  uint8_t  status;      // AckStatus
  uint32_t remoteBoot;  // random at every Remote boot
};

// Item copied by the ESP-NOW receive callback into a FreeRTOS queue
struct RxItem {
  uint8_t  mac[6];
  uint8_t  len;
  uint8_t  data[16];
  uint32_t rxMs;
};

// True when order B is newer than order A (same masterBoot, 16-bit wrap safe)
inline bool seqNewer(uint16_t b, uint16_t a) { return (int16_t)(uint16_t)(b - a) > 0; }
