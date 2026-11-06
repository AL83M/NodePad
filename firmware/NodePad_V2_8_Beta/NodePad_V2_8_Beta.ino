/*
  NodePad - Off-Grid Mesh Terminal for M5Stack Cardputer
  Version: V3.0 development

  UI:
    - 5 second NodePad boot splash
    - Blue/dark NodePad interface
    - Public chat
    - Private Chats contact page + direct chat
    - Nodes
    - Ping Repeaters (all known repeaters)
    - Live Repeaters (bidirectional discovery + SNR/RSSI)
    - Scrollable main menu
    - Advert (Zero-hop / Flood) with TX animation
    - Cardputer battery
    - System

  BLE:
    - Secure NimBLE client
    - MeshCore Nordic-UART-like service
    - Automatic reconnect
    - APP_START after encrypted connection
    - Select PIN on Cardputer, save per Companion after successful pairing
    - Three notification volume levels in System (M): LOW, MEDIUM, MUTE

  Tested target API:
    - M5Cardputer current library
    - NimBLE-Arduino 2.x

  IMPORTANT:
    The "Ping Repeaters" feature uses MeshCore CMD_SEND_PATH_DISCOVERY_REQ (52).
    It is a MeshCore path-discovery request, not an IP/ICMP ping.
*/

#include <Arduino.h>
#include <M5Cardputer.h>
#include <M5GFX.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include <ctype.h>
#include <mbedtls/sha256.h>

// ============================================================
// USER CONFIG
// ============================================================

// MeshCore default pairing code for headless Companion builds (e.g. XIAO).
// If you customized its PIN, use that value instead.
static const uint32_t DEFAULT_MESHCORE_BLE_PIN = 123456;
static const uint8_t SOFT_NOTIFICATION_VOLUME = 12; // low, M5Unified scale 0..255
static const uint8_t MEDIUM_NOTIFICATION_VOLUME = 40; // medium
static const char* APP_NAME = "NodePad";
static const char* APP_VERSION = "V3.0 DEV";

// MeshCore device time is UTC.
// October in Italy is UTC+2; change this to 1 during standard time.
static const int32_t LOCAL_UTC_OFFSET_SECONDS = 2 * 3600;

// ============================================================
// MESHCORE BLE UUIDs
// ============================================================

static NimBLEUUID MESH_SERVICE_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID MESH_RX_UUID     ("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"); // app -> radio
static NimBLEUUID MESH_TX_UUID     ("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"); // radio -> app

// ============================================================
// MESHCORE COMMANDS / RESPONSES
// ============================================================

static const uint8_t CMD_APP_START               = 0x01;
static const uint8_t CMD_SEND_TXT_MSG            = 0x02;
static const uint8_t CMD_SEND_CHANNEL_MSG        = 0x03;
static const uint8_t CMD_GET_CONTACTS            = 0x04;
static const uint8_t CMD_GET_DEVICE_TIME         = 0x05;
static const uint8_t CMD_SEND_SELF_ADVERT        = 0x07;
static const uint8_t CMD_SET_ADVERT_NAME          = 0x08;
static const uint8_t CMD_SET_DEFAULT_FLOOD_SCOPE  = 0x3F;
static const uint8_t CMD_GET_DEFAULT_FLOOD_SCOPE  = 0x40;
static const uint8_t RESP_DEFAULT_FLOOD_SCOPE     = 0x1C;
static const uint8_t CMD_SYNC_NEXT_MESSAGE       = 0x0A;
static const uint8_t CMD_SEND_PATH_DISCOVERY_REQ = 52;
static const uint8_t CMD_SEND_CONTROL_DATA        = 55;

static const uint8_t RESP_OK                     = 0x00;
static const uint8_t RESP_ERR                    = 0x01;
static const uint8_t RESP_CONTACTS_START         = 0x02;
static const uint8_t RESP_CONTACT                = 0x03;
static const uint8_t RESP_END_CONTACTS           = 0x04;
static const uint8_t RESP_SELF_INFO              = 0x05;
static const uint8_t RESP_SENT                   = 0x06;
static const uint8_t RESP_CONTACT_MSG            = 0x07;
static const uint8_t RESP_CHANNEL_MSG            = 0x08;
static const uint8_t RESP_CURRENT_TIME           = 0x09;
static const uint8_t RESP_NO_MORE_MESSAGES       = 0x0A;
static const uint8_t RESP_CONTACT_MSG_V3         = 0x10;
static const uint8_t RESP_CHANNEL_MSG_V3         = 0x11;

static const uint8_t PUSH_ADVERT                 = 0x80;
static const uint8_t PUSH_SEND_CONFIRMED         = 0x82;
static const uint8_t PUSH_MESSAGE_WAITING        = 0x83;
static const uint8_t PUSH_NEW_ADVERT             = 0x8A;
static const uint8_t PUSH_PATH_DISCOVERY         = 0x8D;
static const uint8_t PUSH_CONTROL_DATA           = 0x8E;

static const uint8_t CONTROL_DISCOVER_REQ        = 0x80;
static const uint8_t CONTROL_DISCOVER_RESP       = 0x90;

// Contact advertisement type
static const uint8_t ADV_TYPE_CHAT               = 1;
static const uint8_t ADV_TYPE_REPEATER           = 2;
static const uint8_t ADV_TYPE_ROOM               = 3;
static const uint8_t ADV_TYPE_SENSOR             = 4;

// ============================================================
// DISPLAY / UI
// ============================================================

auto& LCD = M5Cardputer.Display;
M5Canvas D(&M5Cardputer.Display);

uint16_t C_BG;
uint16_t C_PANEL;
uint16_t C_PANEL_SELECTED;
uint16_t C_BLUE;
uint16_t C_BLUE_DIM;
uint16_t C_HEADER;
uint16_t C_WHITE;
uint16_t C_MUTED;

enum ScreenId {
  SCREEN_MAIN,
  SCREEN_CHAT,
  SCREEN_PRIVATE_LIST,
  SCREEN_PRIVATE_CHAT,
  SCREEN_NODES,
  SCREEN_PING,
  SCREEN_LIVE_REPEATERS,
  SCREEN_ADVERT,
  SCREEN_SYSTEM,
  SCREEN_BLE_DEVICES,
  SCREEN_BLE_PIN,
  SCREEN_WEB_SETUP,
  SCREEN_CREDITS
};

ScreenId currentScreen = SCREEN_MAIN;
ScreenId privateReturnScreen = SCREEN_PRIVATE_LIST;
int mainSelection = 0;
int privateListSelection = 0;
int nodesSelection = 0;
int pingSelection = 0;
int liveRepeaterSelection = 0;
int advertSelection = 0;

volatile bool uiDirty = true;
uint32_t lastUiRefresh = 0;

// ============================================================
// BLE STATE
// ============================================================

NimBLEClient* meshClient = nullptr;
NimBLERemoteCharacteristic* meshRX = nullptr;
NimBLERemoteCharacteristic* meshTX = nullptr;

volatile bool bleConnected = false;
volatile bool bleSecure = false;
volatile bool meshOnline = false;

uint32_t lastReconnectAttempt = 0;
uint32_t appStartSentAt = 0;

// Manual switching must never trigger the normal auto-reconnect loop.
bool bleManualSelection = false;
bool bleScanRequested = false;
bool bleScanInProgress = false;
uint32_t bleScanStartedAt = 0;
bool bleAutoReconnect = false;
bool bleConnectRequested = false;
int bleListSelection = 0;
int bleConnectChoice = -1;
static const int MAX_BLE_CHOICES = 12;
struct BleChoice {
  String address;
  String name;
  int rssi;
  uint8_t addressType; // Critical: BLE public (0) vs random (1)
};
BleChoice bleChoices[MAX_BLE_CHOICES];
int bleChoiceCount = 0;
String preferredBleAddress = ""; // Saved radio in ESP32 non-volatile settings.
Preferences nodePadPreferences;
// At startup automatically connect to a single discovered Companion;
// otherwise prefer the radio previously selected by the user.
bool bleConnectAutomatically = false;
char blePickerStatus[48] = "R = RESCAN";
uint32_t manualDisconnectAt = 0;

// PIN is selected on the Cardputer BEFORE the blocking BLE pairing starts.
// This avoids trying to read the keyboard from inside the NimBLE callback.
volatile uint32_t activeBlePasskey = DEFAULT_MESHCORE_BLE_PIN;
String blePinInput = "123456";
bool blePinReplaceOnDigit = true;
char blePinStatus[44] = "ENTER PIN FOR COMPANION";

// Sound level: 0=mute, 1=low, 2=medium. Cycle with M in System.
uint8_t notificationSoundLevel = 1;
uint8_t selectedNotificationVolume() {
  return notificationSoundLevel == 0 ? 0 :
         notificationSoundLevel == 2 ? MEDIUM_NOTIFICATION_VOLUME : SOFT_NOTIFICATION_VOLUME;
}

// Notification audio is requested by BLE callbacks but played from loop().
// The first queue drain after connection is silent (no beep for old messages).
volatile bool notificationBeepRequested = false;
bool notificationsArmed = false;
uint32_t lastBeepAt = 0;

// ============================================================
// RADIO / SELF INFO
// ============================================================

char meshNodeName[40] = "MeshCore";
float radioFreqMHz = 0.0f;
float radioBwKHz = 0.0f;
uint8_t radioSF = 0;
uint8_t radioCR = 0;

// NodePad v2.8: opt-in Wi-Fi SoftAP, using the existing BLE Companion connection.
WebServer radioWeb(80);
bool webActive = false;
String webSsid, webPassword, webToken;
char radioRegionName[32] = "";
bool regionKnown = false;
bool regionQueryPending = false;
bool regionQueryAwaiting = false;
uint32_t regionQueryAt = 0;
enum SaveKind { SAVE_RADIO, SAVE_NAME, SAVE_REGION };
SaveKind saveKind = SAVE_RADIO;
char nextName[32] = "";
char nextRegion[31] = "";
enum SaveStage { SAVE_IDLE, SAVE_QUEUED, SAVE_WAIT_ACK, SAVE_OK, SAVE_ERR, SAVE_TIMEOUT };
SaveStage saveStage = SAVE_IDLE;
volatile int8_t saveAck = 0;
uint32_t saveStart = 0;
float nextFrequency = 0, nextBandwidth = 0;
uint8_t nextSf = 0, nextCr = 0;
char saveDetail[56] = "No settings sent";


// ============================================================
// DEVICE TIME
// ============================================================

uint32_t deviceEpochUTC = 0;
uint32_t deviceEpochMillis = 0;

// ============================================================
// CONTACTS
// ============================================================

static const int MAX_CONTACTS = 48;

struct MeshContact {
  uint8_t pubkey[32];
  uint8_t type;
  char name[33];
  uint32_t lastAdvert;
};

MeshContact contacts[MAX_CONTACTS];
volatile int contactCount = 0;
volatile bool contactsLoaded = false;
volatile bool contactsDirty = false;

// ============================================================
// CARDPUTER BATTERY + RECENTLY HEARD REPEATERS
// ============================================================

int batteryPercent = -1;
uint32_t lastBatteryRead = 0;

static const int MAX_HEARD_NODES = 32;
static const uint32_t REPEATER_ACTIVE_WINDOW_MS = 10UL * 60UL * 1000UL;

struct HeardNode {
  uint8_t pubkey[32];
  uint32_t lastSeenMs;
};

HeardNode heardNodes[MAX_HEARD_NODES];
int heardNodeCount = 0;

static const int MAX_LIVE_REPEATERS = 16;
static const uint32_t REPEATER_BIDIR_WINDOW_MS = 2UL * 60UL * 1000UL;

struct LiveRepeaterLink {
  uint8_t pubkey[32];
  int8_t rxSnr4;      // repeater -> us, SNR * 4
  int8_t txSnr4;      // us -> repeater, SNR * 4 reported by repeater
  int8_t rssi;        // repeater -> us, dBm
  uint32_t lastSeenMs;
};

LiveRepeaterLink liveRepeaters[MAX_LIVE_REPEATERS];
int liveRepeaterCount = 0;
uint32_t repeaterDiscoverTag = 0;
bool repeaterScanning = false;
bool repeaterScanRequested = false;
uint32_t repeaterScanUntil = 0;
uint32_t lastRepeaterScanAt = 0;
char repeaterScanStatus[48] = "R = LIVE SCAN";

// ============================================================
// PROTOCOL STATE / COMMAND SERIALIZATION
// ============================================================

enum ProtocolStage {
  PROTO_WAIT_SELF,
  PROTO_NEED_CONTACTS,
  PROTO_SYNC_CONTACTS_INIT,
  PROTO_NEED_TIME,
  PROTO_WAIT_TIME,
  PROTO_READY,
  PROTO_SYNC_CONTACTS_USER
};

volatile ProtocolStage protocolStage = PROTO_WAIT_SELF;
uint32_t protocolStageStarted = 0;

// ============================================================
// PING / PATH DISCOVERY
// ============================================================

volatile bool pingPending = false;
uint8_t pingTargetPrefix[6] = {0};
uint32_t pingStartedAt = 0;
uint32_t pingTimeoutMs = 10000;
char pingStatus[48] = "SELECT A REPEATER";

// ============================================================
// ADVERT
// ============================================================

volatile bool advertPending = false;
uint32_t advertStartedAt = 0;
char advertStatus[48] = "SELECT ADVERT TYPE";
uint32_t advertSuccessUntil = 0;
uint32_t lastAdvertAnimTick = 0;
uint8_t advertAnimPhase = 0;

// ============================================================
// CHAT
// ============================================================

String chatInput = "";
char lastChatMessage[96] = "No messages yet";
char chatStatus[40] = "READY";

enum ChatPendingKind {
  CHAT_PENDING_NONE,
  CHAT_PENDING_PUBLIC,
  CHAT_PENDING_PRIVATE
};

volatile bool chatPending = false;
volatile ChatPendingKind chatPendingKind = CHAT_PENDING_NONE;
volatile bool messageWaiting = false;
volatile bool messagePollInFlight = false;

static const int MAX_PRIVATE_MESSAGES = 18;

struct PrivateMessage {
  uint8_t peerPrefix[6];
  bool outgoing;
  int8_t snr4;
  uint32_t timestamp;
  char text[80];
};

PrivateMessage privateMessages[MAX_PRIVATE_MESSAGES];
int privateMessageCount = 0;
int privateContactIndex = -1;
String privateInput = "";
char privateStatus[48] = "READY";
bool privateUnread = false;
uint8_t privateUnreadPrefix[6] = {0};

uint32_t privateExpectedAck = 0;
bool privateAckWaiting = false;
uint32_t privateAckStartedAt = 0;
uint32_t privateAckTimeoutMs = 10000;

// BUSY watchdogs.
// A lost/missed BLE response must never leave the terminal locked forever.
uint32_t chatStartedAt = 0;
uint32_t messagePollStartedAt = 0;

static const uint32_t CHAT_BUSY_TIMEOUT_MS = 8000;
static const uint32_t MESSAGE_POLL_TIMEOUT_MS = 3500;

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

void drawCurrentScreen();
void drawConnectionScreen(const char* line1, const char* line2 = nullptr);
bool connectMeshCore(const String& wantedAddress = "", bool cachedResults = false);
void scanMeshCoreDevices();
void finishBleScan();
void startBleDeviceSelection();
void drawBleDevicesScreen();
void drawBlePinScreen();
void openBlePinScreen(int choiceIndex);
void submitBlePin();
bool sendFrame(const uint8_t* data, size_t len);
void sendAppStart();
void handleFrame(const uint8_t* data, size_t len);
void serviceProtocol();
void requestContactsRefresh();
bool userCommandBusy();
void setScreen(ScreenId s);
void goBack();
void handleKeyboard();
void sendPingToSelectedRepeater();
void sendAdvert(bool flood);
void sendChatMessage();
void sendPrivateMessage();
void openPrivateChat(int contactIndex);
void parseIncomingPrivateMessage(const uint8_t* data, size_t len);
void sendRepeaterDiscovery();
void parseContactFrame(const uint8_t* data, size_t len);
void parseSelfInfo(const uint8_t* data, size_t len);
int getRepeaterCount();
int getRepeaterContactIndex(int repeaterListIndex);
int getChatContactCount();
int getChatContactIndex(int chatListIndex);
void sendPathToSelectedLiveRepeater();
void updateBattery(bool force = false);
void noteHeardNode(const uint8_t* pubkey);
bool isRepeaterAttachedByContactIndex(int contactIndex);
int getAttachedRepeaterCount();
int findContactByPrefix(const uint8_t* prefix, int prefixLen);
void addPrivateMessage(const uint8_t* prefix, bool outgoing, int8_t snr4, uint32_t timestamp, const char* text);
int findLiveRepeaterIndex(const uint8_t* pubkey);
bool isRepeaterBidirByContactIndex(int contactIndex);
bool isRepeaterHeardNowByContactIndex(int contactIndex);
int getLiveRepeaterCount();
int getLiveRepeaterContactIndex(int liveListIndex);

// ============================================================
// HELPERS
// ============================================================

static uint32_t readLE32(const uint8_t* p) {
  return ((uint32_t)p[0]) |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void writeLE32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static const char* typeName(uint8_t type) {
  switch (type) {
    case ADV_TYPE_CHAT:     return "CHAT";
    case ADV_TYPE_REPEATER: return "RPT";
    case ADV_TYPE_ROOM:     return "ROOM";
    case ADV_TYPE_SENSOR:   return "SENSOR";
    default:                return "NODE";
  }
}

static bool samePubKey(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, 32) == 0;
}

int findContactByPrefix(const uint8_t* prefix, int prefixLen) {
  if (!prefix || prefixLen <= 0 || prefixLen > 32) return -1;
  for (int i = 0; i < contactCount; ++i) {
    if (memcmp(contacts[i].pubkey, prefix, prefixLen) == 0) return i;
  }
  return -1;
}

void addPrivateMessage(const uint8_t* prefix, bool outgoing, int8_t snr4, uint32_t timestamp, const char* text) {
  if (!prefix || !text) return;

  if (privateMessageCount >= MAX_PRIVATE_MESSAGES) {
    for (int i = 1; i < MAX_PRIVATE_MESSAGES; ++i) {
      privateMessages[i - 1] = privateMessages[i];
    }
    privateMessageCount = MAX_PRIVATE_MESSAGES - 1;
  }

  PrivateMessage& m = privateMessages[privateMessageCount++];
  memcpy(m.peerPrefix, prefix, 6);
  m.outgoing = outgoing;
  m.snr4 = snr4;
  m.timestamp = timestamp;
  strncpy(m.text, text, sizeof(m.text) - 1);
  m.text[sizeof(m.text) - 1] = '\0';
}

int findLiveRepeaterIndex(const uint8_t* pubkey) {
  if (!pubkey) return -1;
  for (int i = 0; i < liveRepeaterCount; ++i) {
    if (samePubKey(liveRepeaters[i].pubkey, pubkey)) return i;
  }
  return -1;
}

static uint32_t currentDeviceEpochUTC() {
  if (deviceEpochUTC == 0) return 0;
  return deviceEpochUTC + ((millis() - deviceEpochMillis) / 1000);
}

static void getClockString(char* out, size_t outLen) {
  uint32_t utc = currentDeviceEpochUTC();
  if (utc == 0) {
    snprintf(out, outLen, "--:--:--");
    return;
  }

  int64_t local = (int64_t)utc + LOCAL_UTC_OFFSET_SECONDS;
  if (local < 0) local = 0;
  uint32_t secDay = (uint32_t)(local % 86400);
  uint8_t hh = secDay / 3600;
  uint8_t mm = (secDay % 3600) / 60;
  uint8_t ss = secDay % 60;
  snprintf(out, outLen, "%02u:%02u:%02u", hh, mm, ss);
}

static void safeCopyText(char* dst, size_t dstLen, const uint8_t* src, size_t srcLen) {
  if (dstLen == 0) return;
  size_t n = srcLen;
  if (n >= dstLen) n = dstLen - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';

  // Strip control characters except normal spaces.
  for (size_t i = 0; i < n; ++i) {
    if ((uint8_t)dst[i] < 0x20 && dst[i] != ' ') dst[i] = ' ';
  }
}

bool userCommandBusy() {
  // messagePollInFlight is background work and must not freeze the UI.
  // Explicit user commands still serialize against ping/advert/chat.
  return pingPending || advertPending || chatPending;
}

void updateBattery(bool force) {
  if (!force && millis() - lastBatteryRead < 10000UL) return;

  int raw = M5Cardputer.Power.getBatteryLevel();
  if (raw >= 0) {
    if (raw > 100) raw = 100;
    if (batteryPercent < 0) {
      batteryPercent = raw;
    } else {
      // Light smoothing so the ADC estimate does not jump around on screen.
      batteryPercent = (batteryPercent * 3 + raw + 2) / 4;
    }
  }

  lastBatteryRead = millis();
}

void noteHeardNode(const uint8_t* pubkey) {
  if (!pubkey) return;

  for (int i = 0; i < heardNodeCount; ++i) {
    if (samePubKey(heardNodes[i].pubkey, pubkey)) {
      heardNodes[i].lastSeenMs = millis();
      return;
    }
  }

  if (heardNodeCount < MAX_HEARD_NODES) {
    memcpy(heardNodes[heardNodeCount].pubkey, pubkey, 32);
    heardNodes[heardNodeCount].lastSeenMs = millis();
    heardNodeCount++;
    return;
  }

  // Replace the oldest slot if the table is full.
  int oldest = 0;
  for (int i = 1; i < MAX_HEARD_NODES; ++i) {
    if (heardNodes[i].lastSeenMs < heardNodes[oldest].lastSeenMs) oldest = i;
  }

  memcpy(heardNodes[oldest].pubkey, pubkey, 32);
  heardNodes[oldest].lastSeenMs = millis();
}

bool isRepeaterAttachedByContactIndex(int contactIndex) {
  if (contactIndex < 0 || contactIndex >= contactCount) return false;
  if (contacts[contactIndex].type != ADV_TYPE_REPEATER) return false;

  for (int i = 0; i < heardNodeCount; ++i) {
    if (samePubKey(contacts[contactIndex].pubkey, heardNodes[i].pubkey)) {
      return (millis() - heardNodes[i].lastSeenMs) <= REPEATER_ACTIVE_WINDOW_MS;
    }
  }

  return false;
}

int getAttachedRepeaterCount() {
  int n = 0;
  for (int i = 0; i < contactCount; ++i) {
    if (isRepeaterAttachedByContactIndex(i)) n++;
  }
  return n;
}

bool isRepeaterBidirByContactIndex(int contactIndex) {
  if (contactIndex < 0 || contactIndex >= contactCount) return false;
  if (contacts[contactIndex].type != ADV_TYPE_REPEATER) return false;

  int idx = findLiveRepeaterIndex(contacts[contactIndex].pubkey);
  if (idx < 0) return false;
  return (millis() - liveRepeaters[idx].lastSeenMs) <= REPEATER_BIDIR_WINDOW_MS;
}

bool isRepeaterHeardNowByContactIndex(int contactIndex) {
  if (contactIndex < 0 || contactIndex >= contactCount) return false;
  if (contacts[contactIndex].type != ADV_TYPE_REPEATER) return false;

  for (int i = 0; i < heardNodeCount; ++i) {
    if (samePubKey(contacts[contactIndex].pubkey, heardNodes[i].pubkey)) {
      return (millis() - heardNodes[i].lastSeenMs) <= REPEATER_BIDIR_WINDOW_MS;
    }
  }
  return false;
}

int getLiveRepeaterCount() {
  int n = 0;
  for (int i = 0; i < contactCount; ++i) {
    if (contacts[i].type == ADV_TYPE_REPEATER &&
        (isRepeaterHeardNowByContactIndex(i) || isRepeaterBidirByContactIndex(i))) {
      n++;
    }
  }
  return n;
}

int getLiveRepeaterContactIndex(int liveListIndex) {
  int n = 0;
  for (int i = 0; i < contactCount; ++i) {
    if (contacts[i].type == ADV_TYPE_REPEATER &&
        (isRepeaterHeardNowByContactIndex(i) || isRepeaterBidirByContactIndex(i))) {
      if (n == liveListIndex) return i;
      n++;
    }
  }
  return -1;
}

// ============================================================
// BLE CALLBACKS
// ============================================================

class MeshClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pClient) override {
    bleConnected = true;
    uiDirty = true;
    Serial.println("[BLE] Connected");
  }

  void onDisconnect(NimBLEClient* pClient, int reason) override {
    Serial.printf("[BLE] Disconnected. reason=%d\n", reason);
    bleConnected = false;
    bleSecure = false;
    meshOnline = false;
    meshRX = nullptr;
    meshTX = nullptr;

    protocolStage = PROTO_WAIT_SELF;
    contactsLoaded = false;
    pingPending = false;
    advertPending = false;
    chatPending = false;
    chatPendingKind = CHAT_PENDING_NONE;
    messagePollInFlight = false;
    chatStartedAt = 0;
    messagePollStartedAt = 0;
    privateAckWaiting = false;
    repeaterScanning = false;
    repeaterScanRequested = false;
    regionKnown = false;
    regionQueryAwaiting = false;
    regionQueryPending = webActive;
    notificationsArmed = false;
    notificationBeepRequested = false;

    snprintf(pingStatus, sizeof(pingStatus), "DISCONNECTED");
    snprintf(advertStatus, sizeof(advertStatus), "DISCONNECTED");
    snprintf(chatStatus, sizeof(chatStatus), "DISCONNECTED");
    snprintf(privateStatus, sizeof(privateStatus), "DISCONNECTED");
    snprintf(repeaterScanStatus, sizeof(repeaterScanStatus), "DISCONNECTED");
    uiDirty = true;
  }

  void onPassKeyEntry(NimBLEConnInfo& connInfo) override {
    Serial.println("[BLE] Passkey requested -> injecting selected PIN");
    NimBLEDevice::injectPassKey(connInfo, activeBlePasskey);
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    bleSecure = connInfo.isEncrypted();
    Serial.printf("[BLE] Authentication complete. encrypted=%s\n",
                  bleSecure ? "YES" : "NO");

    if (!bleSecure) {
      NimBLEClient* c = NimBLEDevice::getClientByHandle(connInfo.getConnHandle());
      if (c) c->disconnect();
    }
    uiDirty = true;
  }
};

MeshClientCallbacks meshClientCallbacks;

void meshNotifyCallback(
  NimBLERemoteCharacteristic* characteristic,
  uint8_t* data,
  size_t len,
  bool isNotify
) {
  if (!data || len == 0) return;

  Serial.printf("[RX] 0x%02X len=%u\n", data[0], (unsigned)len);
  handleFrame(data, len);
}

// ============================================================
// BLE CONNECTION
// ============================================================

bool sendFrame(const uint8_t* data, size_t len) {
  if (!bleConnected || !meshClient || !meshClient->isConnected() || !meshRX) {
    Serial.println("[TX] Not connected");
    return false;
  }

  bool ok = false;

  if (meshRX->canWrite()) {
    ok = meshRX->writeValue(data, len, true);
  } else if (meshRX->canWriteNoResponse()) {
    ok = meshRX->writeValue(data, len, false);
  }

  if (ok) {
    Serial.printf("[TX] 0x%02X len=%u OK\n", data[0], (unsigned)len);
  } else {
    Serial.printf("[TX] 0x%02X len=%u FAILED\n", data[0], (unsigned)len);
  }

  return ok;
}


const char* stageLabel() {
  switch(saveStage) {
    case SAVE_QUEUED: return "queued";
    case SAVE_WAIT_ACK: return "waiting";
    case SAVE_OK: return "saved";
    case SAVE_ERR: return "error";
    case SAVE_TIMEOUT: return "timeout";
    default: return "idle";
  }
}

String jsonSafe(String text) {
  text.replace("\\", "\\\\"); text.replace("\"", "\\\"");
  text.replace("\n", " "); text.replace("\r", " ");
  return text;
}
void sendJson(int status, const String& s) {
  radioWeb.sendHeader("Cache-Control","no-store");
  radioWeb.send(status, "application/json", s);
}
void radioStatusHttp() {
  String j="{\"online\":";
  j+=(meshOnline && bleConnected ? "true" : "false");
  j+=",\"name\":\""+jsonSafe(String(meshNodeName))+"\"";
  j+=",\"region\":\""+jsonSafe(String(radioRegionName))+"\"";
  j+=",\"regionKnown\":";
  j+=(regionKnown ? "true" : "false");
  j+=",\"freq\":"+String(radioFreqMHz,3);
  j+=",\"bw\":"+String(radioBwKHz,3);
  j+=",\"sf\":"+String(radioSF)+",\"cr\":"+String(radioCR);
  j+=",\"state\":\""+String(stageLabel())+"\"";
  j+=",\"detail\":\""+jsonSafe(String(saveDetail))+"\"}";
  sendJson(200,j);
}
bool allowedBW(float x) {
  const float values[]={7.8f,10.4f,15.6f,20.8f,31.25f,41.7f,62.5f,125.0f,250.0f,500.0f};
  for(float v:values) if(fabsf(v-x)<0.055f) return true;
  return false;
}
void radioApplyHttp() {
  if(!webActive || !radioWeb.hasArg("token") || radioWeb.arg("token")!=webToken) {
    sendJson(403,"{\"error\":\"Invalid token\"}"); return;
  }
  if(!bleConnected || !meshOnline || protocolStage!=PROTO_READY) {
    sendJson(409,"{\"error\":\"Companion not ready\"}"); return;
  }
  if(saveStage==SAVE_QUEUED || saveStage==SAVE_WAIT_ACK ||
     userCommandBusy() || messagePollInFlight || pingPending || advertPending ||
     repeaterScanning) {
    sendJson(409,"{\"error\":\"Radio is busy\"}"); return;
  }
  if(!radioWeb.hasArg("frequency") || !radioWeb.hasArg("bandwidth") ||
     !radioWeb.hasArg("sf") || !radioWeb.hasArg("cr") ||
     !radioWeb.hasArg("confirmed") || radioWeb.arg("confirmed")!="yes") {
    sendJson(400,"{\"error\":\"Missing fields or confirmation\"}"); return;
  }
  float f=radioWeb.arg("frequency").toFloat();
  float b=radioWeb.arg("bandwidth").toFloat();
  int sf=radioWeb.arg("sf").toInt(), cr=radioWeb.arg("cr").toInt();
  if(!isfinite(f) || f<300 || f>2500 || !allowedBW(b) || sf<5 || sf>12 || cr<5 || cr>8) {
    sendJson(422,"{\"error\":\"Invalid radio parameters\"}"); return;
  }
  nextFrequency=f; nextBandwidth=b; nextSf=sf; nextCr=cr;
  saveKind=SAVE_RADIO;
  saveAck=0; saveStage=SAVE_QUEUED;
  snprintf(saveDetail,sizeof(saveDetail),"Queued for BLE Companion");
  sendJson(202,"{\"state\":\"queued\"}");
  uiDirty=true;
}

bool configWriteAllowed() {
  return bleConnected && meshOnline && protocolStage==PROTO_READY &&
         saveStage!=SAVE_QUEUED && saveStage!=SAVE_WAIT_ACK &&
         !regionQueryAwaiting && !userCommandBusy() &&
         !messagePollInFlight && !pingPending && !advertPending &&
         !repeaterScanning;
}
bool checkConfigToken() {
  if(!webActive || !radioWeb.hasArg("token") ||
     radioWeb.arg("token")!=webToken) {
    sendJson(403,"{\"error\":\"Invalid token\"}");
    return false;
  }
  if(!configWriteAllowed()) {
    sendJson(409,"{\"error\":\"Companion offline or busy - try again\"}");
    return false;
  }
  return true;
}
// Only plain UTF-8 ASCII names, so byte limits match MeshCore frame limits.
bool validNodeName(const String& s) {
  if(s.length()<1 || s.length()>31 || s[0]==' ' || s[s.length()-1]==' ') return false;
  for(unsigned i=0;i<s.length();i++) {
    const char ch=s[i];
    if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
         (ch>='0'&&ch<='9')||ch==' '||ch=='_'||ch=='-'||ch=='.')) return false;
  }
  return true;
}
bool validRegionName(const String& s) {
  if(s.length()>30) return false; // Empty means clear/default unscoped.
  for(unsigned i=0;i<s.length();i++) {
    const char ch=s[i];
    if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
         (ch>='0'&&ch<='9')||ch=='_'||ch=='-')) return false;
  }
  return true;
}
void nameApplyHttp() {
  if(!checkConfigToken()) return;
  if(!radioWeb.hasArg("node_name") || !radioWeb.hasArg("confirmed") ||
     radioWeb.arg("confirmed")!="yes") {
    sendJson(400,"{\"error\":\"Missing name or confirmation\"}"); return;
  }
  String s=radioWeb.arg("node_name"); s.trim();
  if(!validNodeName(s)) {
    sendJson(422,"{\"error\":\"Use 1-31 letters, digits, spaces, dot, - or _\"}"); return;
  }
  s.toCharArray(nextName,sizeof(nextName));
  saveKind=SAVE_NAME; saveAck=0; saveStage=SAVE_QUEUED;
  snprintf(saveDetail,sizeof(saveDetail),"Name update queued");
  sendJson(202,"{\"state\":\"queued\"}");
}
void regionApplyHttp() {
  if(!checkConfigToken()) return;
  if(!radioWeb.hasArg("region") || !radioWeb.hasArg("confirmed") ||
     radioWeb.arg("confirmed")!="yes") {
    sendJson(400,"{\"error\":\"Missing scope or confirmation\"}"); return;
  }
  String s=radioWeb.arg("region"); s.trim();
  if(!validRegionName(s)) {
    sendJson(422,"{\"error\":\"Use 0-30 letters, digits, - or _\"}"); return;
  }
  s.toCharArray(nextRegion,sizeof(nextRegion));
  saveKind=SAVE_REGION; saveAck=0; saveStage=SAVE_QUEUED;
  snprintf(saveDetail,sizeof(saveDetail),"Region scope queued");
  sendJson(202,"{\"state\":\"queued\"}");
}

const char setupHtml[] PROGMEM = R"NODEPAGE(
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>NodePad Radio Setup</title>
<style>
:root{color-scheme:dark;font:16px system-ui;background:#091223;color:#e7edff}
body{max-width:600px;margin:auto;padding:20px}
section{background:#12213b;border:1px solid #34537e;border-radius:16px;padding:20px;margin:18px 0}
h1{color:#77beff}p{line-height:1.5;color:#bac9df}
label{display:block;margin-top:15px}input,select,button{width:100%;padding:11px;margin-top:6px;
font:inherit;border:1px solid #49658c;border-radius:8px;background:#0a1931;color:white}
button{background:#267eef;border:0;margin-top:24px;font-weight:bold}
button:disabled{opacity:.4}.row{display:grid;grid-template-columns:1fr 1fr;gap:14px}
#confirm{width:auto}a{color:#7fc0ff}
</style></head><body><p>NODEPAD / V2.8 BETA</p><h1>Radio Settings</h1>
<p>The Cardputer hosts this page locally and configures your MeshCore Companion over BLE.</p>
<section><b id="online">Connecting...</b><p id="current"></p><p id="status"></p></section>
<section><h2>Companion Name</h2><form id="nameForm">
<label>Advertised node name
<input name="node_name" id="node_name" maxlength="31" pattern="[A-Za-z0-9 _.-]{1,31}" required></label>
<p>This changes the name announced by your connected MeshCore Companion.</p>
<button id="nameSave" disabled>Save Name</button></form></section>
<section><h2>Default Region Scope</h2><form id="regionForm">
<p>This is a MeshCore flood scope, NOT a LoRa frequency band.
Use exactly the region name accepted by your repeaters.</p>
<label>Region (e.g. your mesh's region code)
<input name="region" id="region" maxlength="30" pattern="[A-Za-z0-9_-]{0,30}" placeholder="Leave empty to clear"></label>
<p id="regionCurrent">Current region: reading...</p>
<label><input type="checkbox" id="regionConfirm" required>
I checked this scope with my mesh. Wrong scopes can prevent messages reaching repeaters.
An empty value removes the default scope.</label>
<button id="regionSave" disabled>Save Default Region</button></form></section>
<section><h2>LoRa Parameters</h2><form id="form">
<label>Frequency (MHz)<input name="frequency" id="frequency" type="number" min="300" max="2500" step=".001" required></label>
<div class="row"><label>Bandwidth (kHz)<select name="bandwidth" id="bandwidth">
<option>7.8</option><option>10.4</option><option>15.6</option><option>20.8</option><option>31.25</option>
<option>41.7</option><option>62.5</option><option>125</option><option>250</option><option>500</option>
</select></label><label>Spreading Factor<select name="sf" id="sf">
<option>5</option><option>6</option><option>7</option><option>8</option><option>9</option>
<option>10</option><option>11</option><option>12</option></select></label></div>
<label>Coding Rate<select name="cr" id="cr">
<option value="5">4/5</option><option value="6">4/6</option>
<option value="7">4/7</option><option value="8">4/8</option></select></label>
<label><input id="confirm" type="checkbox" required>
I checked that my region, hardware and MeshCore network allow these values.</label>
<button id="save" disabled>Save to Companion</button></form></section>
<p>A saved acknowledgement does NOT mean the new parameters are active.
Reboot the Companion if required. Unsupported settings can interrupt mesh connectivity.</p>
<p><a href="/credits">Credits</a></p>
<script>
var token="__TOKEN__",initialized=false,nameInitialized=false,regionInitialized=false;
var $=id=>document.getElementById(id);
async function refresh(){
 try{
  var r=await fetch("/api/status",{cache:"no-store"}),d=await r.json();
  $("online").textContent=d.online?"BLE connected: "+d.name:"Companion offline";
  $("current").textContent="Current: "+d.freq+" MHz · "+d.bw+" kHz · SF"+d.sf+" CR"+d.cr;
  $("status").textContent=d.detail+" ("+d.state+")";
  $("regionCurrent").textContent=d.regionKnown?"Current region: "+(d.region||"unscoped"):"Current region: not available (older firmware?)";
  if(d.online&&!nameInitialized){$("node_name").value=d.name;nameInitialized=true;}
  if(d.online&&d.regionKnown&&!regionInitialized){$("region").value=d.region;regionInitialized=true;}
  if(!initialized&&d.online&&d.freq>0){
   $("frequency").value=d.freq;$("bandwidth").value=String(d.bw);
   $("sf").value=String(d.sf);$("cr").value=String(d.cr);initialized=true;
  }
  var busy=!d.online || d.state==="waiting" || d.state==="queued";
  $("save").disabled=busy;$("nameSave").disabled=busy;
  $("regionSave").disabled=busy;
 }catch(e){$("online").textContent="Cardputer unavailable";
 $("save").disabled=true;$("nameSave").disabled=true;$("regionSave").disabled=true}
}
$("form").addEventListener("submit",async e=>{
 e.preventDefault();$("save").disabled=true;
 var p=new URLSearchParams(new FormData($("form")));
 p.append("confirmed","yes");p.append("token",token);
 try{var r=await fetch("/api/radio",{method:"POST",body:p});var d=await r.json();
     if(!r.ok)alert(d.error||"Settings rejected");}
 catch(e){alert("Wi-Fi connection lost")}
 await refresh();
});
function bindForm(formId,endpoint){
 $(formId).addEventListener("submit",async e=>{
  e.preventDefault();
  var p=new URLSearchParams(new FormData($(formId)));
  p.append("confirmed","yes");p.append("token",token);
  try{
   var r=await fetch(endpoint,{method:"POST",body:p}),d=await r.json();
   if(!r.ok)alert(d.error||"Settings rejected");
   else if(formId==="regionForm")$("regionConfirm").checked=false;
  }catch(e){alert("Wi-Fi connection lost")}
  await refresh();
 });
}
bindForm("nameForm","/api/name");
bindForm("regionForm","/api/region");
refresh();setInterval(refresh,1500);
</script></body></html>
)NODEPAGE";

const char creditsHtml[] PROGMEM = R"NODEPAGE(
<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>NodePad Credits</title><style>
body{font:16px system-ui;line-height:1.7;background:#091223;color:#e7edff;
max-width:600px;padding:20px;margin:auto}h1,a{color:#77beff}</style>
<h1>NodePad / Credits</h1>
<p>Project, design and firmware: <b>AL83M</b></p>
<p>Community beta testers: thanks to everyone testing NodePad.</p>
<p>Technology acknowledgements: MeshCore protocol, M5Stack hardware and libraries,
NimBLE-Arduino.</p>
<p>Independent community project; not endorsed by MeshCore or M5Stack.</p>
<p><a href="/">Radio Settings</a></p>
)NODEPAGE";

void configureRadioWeb() {
  radioWeb.on("/", HTTP_GET, [](){
    String page=FPSTR(setupHtml);
    page.replace("__TOKEN__", webToken);
    radioWeb.sendHeader("Cache-Control","no-store");
    radioWeb.send(200,"text/html; charset=utf-8",page);
  });
  radioWeb.on("/credits", HTTP_GET, [](){
    radioWeb.send_P(200,"text/html; charset=utf-8",creditsHtml);
  });
  radioWeb.on("/api/status", HTTP_GET, radioStatusHttp);
  radioWeb.on("/api/radio", HTTP_POST, radioApplyHttp);
  radioWeb.on("/api/name", HTTP_POST, nameApplyHttp);
  radioWeb.on("/api/region", HTTP_POST, regionApplyHttp);
  radioWeb.onNotFound([](){radioWeb.send(404,"text/plain","Not found");});
}
void stopSetupAccessPoint() {
  if(!webActive)return;
  radioWeb.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  webActive=false;
  webPassword=""; webToken="";
  uiDirty=true;
}
bool startSetupAccessPoint() {
  if(webActive)return true;
  char ss[9],pass[17],tok[17];
  snprintf(ss,sizeof(ss),"%04lX",(unsigned long)(ESP.getEfuseMac()&0xFFFF));
  snprintf(pass,sizeof(pass),"%08lX%04lX",
       (unsigned long)esp_random(),(unsigned long)(esp_random()&0xFFFF));
  snprintf(tok,sizeof(tok),"%08lX%08lX",
       (unsigned long)esp_random(),(unsigned long)esp_random());
  webSsid="NodePad-"+String(ss);webPassword=pass;webToken=tok;
  WiFi.mode(WIFI_AP);
  if(!WiFi.softAP(webSsid.c_str(),webPassword.c_str(),1,false,2)) {
    WiFi.mode(WIFI_OFF);return false;
  }
  radioWeb.begin();webActive=true;
  regionKnown=false;regionQueryPending=true;regionQueryAwaiting=false;
  uiDirty=true;return true;
}
// Stop other MeshCore commands while a settings command awaits an ACK.
// BLE callbacks only set saveAck; the loop owns all other state changes.
bool serviceRadioSettings() {
  if(saveStage==SAVE_WAIT_ACK) {
    if(saveAck!=0) {
      if(saveAck==1) {
        saveStage=SAVE_OK;
        if(saveKind==SAVE_RADIO) {
          radioFreqMHz=nextFrequency;radioBwKHz=nextBandwidth;
          radioSF=nextSf;radioCR=nextCr;
          snprintf(saveDetail,sizeof(saveDetail),"Radio saved - restart Companion if needed");
        } else if(saveKind==SAVE_NAME) {
          snprintf(meshNodeName,sizeof(meshNodeName),"%s",nextName);
          snprintf(saveDetail,sizeof(saveDetail),"Node name saved to Companion");
        } else if(saveKind==SAVE_REGION) {
          snprintf(radioRegionName,sizeof(radioRegionName),"%s",nextRegion);
          regionKnown=true;
          snprintf(saveDetail,sizeof(saveDetail),"Default region saved to Companion");
        }
      } else {
        saveStage=SAVE_ERR;
        snprintf(saveDetail,sizeof(saveDetail),"Companion rejected setting or firmware too old");
      }
      saveAck=0;uiDirty=true;return false;
    }
    if(millis()-saveStart>9000) {
      saveStage=SAVE_TIMEOUT;
      snprintf(saveDetail,sizeof(saveDetail),"ACK timeout - verify before retry");
      uiDirty=true;return false;
    }
    return true;
  }
  if(saveStage!=SAVE_QUEUED)return false;
  if(protocolStage!=PROTO_READY || userCommandBusy() || messagePollInFlight ||
     regionQueryAwaiting || pingPending || advertPending || repeaterScanning)return false;

  uint8_t frame[48]={0};
  size_t frameLen=0;
  if(saveKind==SAVE_NAME) {
    frame[0]=CMD_SET_ADVERT_NAME;
    size_t n=strlen(nextName);
    memcpy(&frame[1],nextName,n);
    frameLen=1+n;
  } else if(saveKind==SAVE_REGION) {
    frame[0]=CMD_SET_DEFAULT_FLOOD_SCOPE;
    frameLen=1; // Empty region resets default scope.
    if(nextRegion[0]!='\0') {
      // MeshCore public region keys are SHA-256("#" + region_name)[:16].
      String scopedName="#"+String(nextRegion);
      uint8_t hash[32]={0};
      if(mbedtls_sha256((const unsigned char*)scopedName.c_str(),
                        scopedName.length(),hash,0)!=0) {
        saveStage=SAVE_ERR;
        snprintf(saveDetail,sizeof(saveDetail),"Region SHA-256 failed");
        return false;
      }
      memcpy(&frame[1],nextRegion,strlen(nextRegion)); // 31-byte zero-padded field.
      memcpy(&frame[32],hash,16);
      frameLen=48;
    }
  } else {
    uint32_t frequency=(uint32_t)lroundf(nextFrequency*1000.0f);
    uint32_t bandwidth=(uint32_t)lroundf(nextBandwidth*1000.0f);
    frame[0]=11;
    for(int i=0;i<4;i++) {
      frame[1+i]=(frequency>>(8*i))&0xFF;
      frame[5+i]=(bandwidth>>(8*i))&0xFF;
    }
    frame[9]=nextSf;frame[10]=nextCr;
    frameLen=11;
  }
  saveAck=0;saveStage=SAVE_WAIT_ACK;saveStart=millis();
  if(!sendFrame(frame,frameLen)) {
    saveStage=SAVE_ERR;
    snprintf(saveDetail,sizeof(saveDetail),"BLE write failed");
    return false;
  }
  snprintf(saveDetail,sizeof(saveDetail),"Awaiting BLE acknowledgement");
  uiDirty=true;return true;
}

void sendAppStart() {
  uint8_t frame[40] = {0};
  frame[0] = CMD_APP_START;

  size_t nameLen = strlen(APP_NAME);
  if (nameLen > sizeof(frame) - 8) nameLen = sizeof(frame) - 8;
  memcpy(&frame[8], APP_NAME, nameLen);

  if (sendFrame(frame, 8 + nameLen)) {
    appStartSentAt = millis();
    protocolStage = PROTO_WAIT_SELF;
  }
}

// ESP32 NVS preference keys are max. 15 characters.
// A 12-hex-digit BLE address plus 'p' is unique per Companion (13 chars).
String blePinStorageKey(const String& address) {
  String key = "p";
  for (size_t i = 0; i < address.length(); ++i) {
    char c = address.charAt(i);
    if (isxdigit((unsigned char)c)) key += (char)tolower((unsigned char)c);
  }
  return key;
}

uint32_t getSavedBlePin(const String& address) {
  String key = blePinStorageKey(address);
  uint32_t saved = nodePadPreferences.getUInt(key.c_str(), DEFAULT_MESHCORE_BLE_PIN);
  if (saved > 999999) return DEFAULT_MESHCORE_BLE_PIN;
  return saved;
}

void openBlePinScreen(int choiceIndex) {
  if (choiceIndex < 0 || choiceIndex >= bleChoiceCount) return;
  bleConnectChoice = choiceIndex;
  uint32_t saved = getSavedBlePin(bleChoices[choiceIndex].address);
  char sixDigits[7];
  snprintf(sixDigits, sizeof(sixDigits), "%06lu", (unsigned long)saved);
  blePinInput = sixDigits;
  blePinReplaceOnDigit = true;
  snprintf(blePinStatus, sizeof(blePinStatus), "TYPE TO REPLACE - ENTER TO PAIR");
  setScreen(SCREEN_BLE_PIN);
}

void submitBlePin() {
  if (blePinInput.length() != 6) {
    snprintf(blePinStatus, sizeof(blePinStatus), "PIN MUST BE 6 DIGITS");
    uiDirty = true;
    return;
  }
  activeBlePasskey = (uint32_t)blePinInput.toInt();
  bleConnectRequested = true;
  bleAutoReconnect = false;
  bleManualSelection = true;
  snprintf(blePickerStatus, sizeof(blePickerStatus), "CONNECT REQUESTED...");
  setScreen(SCREEN_BLE_DEVICES);
}

// Start a NON-BLOCKING scan. Keyboard and LCD remain responsive.
void scanMeshCoreDevices() {
  bleScanRequested = false;
  bleScanInProgress = false;
  bleChoiceCount = 0;
  bleListSelection = 0;
  snprintf(blePickerStatus, sizeof(blePickerStatus), "SEARCHING (8s)...");
  setScreen(SCREEN_BLE_DEVICES);

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(80);
  scan->setWindow(60);
  scan->setMaxResults(96);

  Serial.println("[BLE] Starting async 8s scan");
  bleScanStartedAt = millis();
  if (scan->start(8000, false, true)) {
    bleScanInProgress = true;
  } else {
    Serial.println("[BLE] Scan failed to start");
    snprintf(blePickerStatus, sizeof(blePickerStatus), "SCAN ERROR - PRESS R");
  }
  uiDirty = true;
}

// Called only after scanning has stopped. Results remain valid until
// a new scan/clearResults call, so we copy the device details into Strings.
void finishBleScan() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) scan->stop();
  bleScanInProgress = false;
  bleChoiceCount = 0;
  bleListSelection = 0;

  NimBLEScanResults results = scan->getResults();
  Serial.printf("[BLE] Async scan ended, raw devices=%d\n", results.getCount());
  for (int i = 0; i < results.getCount(); ++i) {
    const NimBLEAdvertisedDevice* dev = results.getDevice(i);
    if (!dev || !dev->isAdvertisingService(MESH_SERVICE_UUID)) continue;
    String addr = String(dev->getAddress().toString().c_str());
    bool exists = false;
    for (int j = 0; j < bleChoiceCount; ++j) {
      if (bleChoices[j].address == addr) { exists = true; break; }
    }
    if (exists || bleChoiceCount >= MAX_BLE_CHOICES) continue;
    BleChoice& choice = bleChoices[bleChoiceCount++];
    choice.address = addr;
    choice.name = String(dev->getName().c_str());
    if (!choice.name.length()) choice.name = "MeshCore " + addr.substring(9);
    choice.rssi = dev->getRSSI();
    choice.addressType = dev->getAddress().getType();
    Serial.printf("[BLE PICK] %s %s (type=%u, %d dBm)\n",
                  choice.name.c_str(), addr.c_str(),
                  (unsigned)choice.addressType, choice.rssi);
  }

  if (bleChoiceCount == 0) {
    snprintf(blePickerStatus, sizeof(blePickerStatus), "NONE FOUND - R: RESCAN");
  } else {
    snprintf(blePickerStatus, sizeof(blePickerStatus), "%d FOUND - ENTER: CONNECT", bleChoiceCount);
  }

  if (bleConnectAutomatically || bleAutoReconnect) {
    // On boot/link loss reconnect only to a known Companion when present.
    // If no previous radio exists, auto-connect only when exactly one radio
    // was found. With multiple new radios, let the user decide.
    int chosen = -1;
    if (preferredBleAddress.length()) {
      for (int i = 0; i < bleChoiceCount; ++i) {
        if (bleChoices[i].address.equalsIgnoreCase(preferredBleAddress)) {
          chosen = i;
          break;
        }
      }
    } else if (bleConnectAutomatically && bleChoiceCount == 1) {
      chosen = 0;
    }
    if (chosen >= 0) {
      bleConnectChoice = chosen;
      activeBlePasskey = getSavedBlePin(bleChoices[chosen].address);
      bleConnectRequested = true;
      snprintf(blePickerStatus, sizeof(blePickerStatus), "AUTO CONNECTING...");
    }
  }
  bleAutoReconnect = false;
  bleConnectAutomatically = false;
  setScreen(SCREEN_BLE_DEVICES);
}

void startBleDeviceSelection() {
  // Set this BEFORE disconnect(), otherwise onDisconnect may initiate a retry.
  bleManualSelection = true;
  bleAutoReconnect = false;
  bleConnectAutomatically = false;
  bleConnectRequested = false;
  bleScanRequested = true;
  bleChoiceCount = 0;
  manualDisconnectAt = millis();
  snprintf(blePickerStatus, sizeof(blePickerStatus), "DISCONNECTING...");
  setScreen(SCREEN_BLE_DEVICES);
  if (meshClient && meshClient->isConnected()) meshClient->disconnect();
}

// Keep the same device-picker screen while connecting: no full-screen
// transitions between CONNECTING, BLE SECURE and the picker.
void showBleConnectStatus(const char* status) {
  snprintf(blePickerStatus, sizeof(blePickerStatus), "%s", status);
  currentScreen = SCREEN_BLE_DEVICES;
  uiDirty = true;
  drawCurrentScreen();
}

// Show an actionable, stable error and retain the device scan cache.
// A failed attempt must not leave a selectable but unresolvable BLE entry.
bool bleConnectionFailed(const char* stage, int code) {
  if (code != 0) {
    snprintf(blePickerStatus, sizeof(blePickerStatus), "%s ERROR %d - R: RESCAN", stage, code);
  } else {
    snprintf(blePickerStatus, sizeof(blePickerStatus), "%s FAILED - R: RESCAN", stage);
  }
  Serial.printf("[BLE] %s failed, rc=%d\n", stage, code);
  bleManualSelection = true;
  uiDirty = true;
  return false;
}

bool connectMeshCore(const String& wantedAddress, bool cachedResults) {
  lastReconnectAttempt = millis();

  // Connect using the selected and cached BLE address + type, not a
  // potentially expired pointer into the NimBLE scan results.
  bleConnected = false;
  bleSecure = false;
  meshOnline = false;
  meshRX = nullptr;
  meshTX = nullptr;
  protocolStage = PROTO_WAIT_SELF;
  notificationsArmed = false;
  notificationBeepRequested = false;

  if (!cachedResults) {
    return bleConnectionFailed("SCAN REQUIRED", 0);
  }

  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) scan->stop();

  // The list is built at scan completion. The NimBLE internal result list
  // can be changed/invalidated after the scan; do NOT try to search it again.
  int selected = -1;
  for (int i = 0; i < bleChoiceCount; ++i) {
    if (bleChoices[i].address.equalsIgnoreCase(wantedAddress)) {
      selected = i;
      break;
    }
  }
  if (selected < 0) return bleConnectionFailed("RESCAN TO SELECT", 0);

  const String connectedAddress = bleChoices[selected].address;
  const uint8_t addressType = bleChoices[selected].addressType;
  const NimBLEAddress selectedAddress(
      std::string(connectedAddress.c_str()), addressType);
  Serial.printf("[BLE] Connecting directly to %s type=%u\n",
                connectedAddress.c_str(), (unsigned)addressType);
  showBleConnectStatus("CONNECTING...");

  if (meshClient) {
    if (meshClient->isConnected()) meshClient->disconnect();
    NimBLEDevice::deleteClient(meshClient);
    meshClient = nullptr;
  }

  meshClient = NimBLEDevice::createClient();
  if (!meshClient) return bleConnectionFailed("CLIENT CREATE", 0);
  meshClient->setClientCallbacks(&meshClientCallbacks, false);
  meshClient->setConnectTimeout(8000);

  if (!meshClient->connect(selectedAddress)) {
    return bleConnectionFailed("CONNECT", meshClient->getLastError());
  }

  // The callback may update connection flags during pairing.
  bleConnected = true;
  showBleConnectStatus("PAIRING - CHECK BLE PIN...");

  if (!meshClient->secureConnection()) {
    int rc = meshClient->getLastError();
    meshClient->disconnect();
    return bleConnectionFailed("PAIR/PIN", rc);
  }

  NimBLEConnInfo secureInfo = meshClient->getConnInfo();
  bleSecure = secureInfo.isEncrypted();
  if (!bleSecure) {
    meshClient->disconnect();
    return bleConnectionFailed("ENCRYPTION", 0);
  }

  showBleConnectStatus("READING MESHCORE SERVICE...");
  NimBLERemoteService* service = meshClient->getService(MESH_SERVICE_UUID);
  if (!service) {
    int rc = meshClient->getLastError();
    meshClient->disconnect();
    return bleConnectionFailed("SERVICE", rc);
  }

  meshRX = service->getCharacteristic(MESH_RX_UUID);
  meshTX = service->getCharacteristic(MESH_TX_UUID);
  if (!meshRX || !meshTX) {
    meshClient->disconnect();
    return bleConnectionFailed("RX/TX MISSING", 0);
  }

  if (!meshTX->canNotify()) {
    meshClient->disconnect();
    return bleConnectionFailed("NO NOTIFY", 0);
  }

  showBleConnectStatus("SUBSCRIBING...");
  if (!meshTX->subscribe(true, meshNotifyCallback)) {
    int rc = meshClient->getLastError();
    meshClient->disconnect();
    return bleConnectionFailed("SUBSCRIBE", rc);
  }

  delay(120);
  showBleConnectStatus("CONNECTED - STARTING...");
  // Save the last Companion only after every connection step succeeds.
  preferredBleAddress = connectedAddress;
  nodePadPreferences.putString("last_radio", preferredBleAddress);
  // Save only a PIN that successfully authenticated with this Companion.
  String pinKey = blePinStorageKey(connectedAddress);
  nodePadPreferences.putUInt(pinKey.c_str(), activeBlePasskey);
  sendAppStart();
  bleManualSelection = false;
  return true;
}

// ============================================================
// PROTOCOL PARSING
// ============================================================

void parseSelfInfo(const uint8_t* data, size_t len) {
  if (len >= 58) {
    radioFreqMHz = readLE32(&data[48]) / 1000.0f;
    radioBwKHz   = readLE32(&data[52]) / 1000.0f;
    radioSF      = data[56];
    radioCR      = data[57];

    if (len > 58) {
      safeCopyText(meshNodeName, sizeof(meshNodeName), &data[58], len - 58);
      if (meshNodeName[0] == '\0') {
        strncpy(meshNodeName, "MeshCore", sizeof(meshNodeName) - 1);
      }
    }
  }

  meshOnline = true;
  protocolStage = PROTO_NEED_CONTACTS;
  protocolStageStarted = millis();

  snprintf(chatStatus, sizeof(chatStatus), "READY");
  snprintf(privateStatus, sizeof(privateStatus), "READY");
  snprintf(pingStatus, sizeof(pingStatus), "SELECT A REPEATER");
  snprintf(repeaterScanStatus, sizeof(repeaterScanStatus), "R = LIVE SCAN");
  snprintf(advertStatus, sizeof(advertStatus), "SELECT ADVERT TYPE");

  uiDirty = true;

  Serial.printf("[MESH] ONLINE node=%s freq=%.3f MHz SF%u CR%u\n",
                meshNodeName, radioFreqMHz, radioSF, radioCR);
}

void parseContactFrame(const uint8_t* data, size_t len) {
  // RESP_CONTACT fixed layout requires at least 148 bytes.
  // 0: code
  // 1..32: pubkey
  // 33: type
  // 34: flags
  // 35: out_path_len
  // 36..99: path[64]
  // 100..131: name[32]
  // 132..135: last advert
  // ...
  if (len < 148) return;

  const uint8_t* pubkey = &data[1];

  int idx = -1;
  for (int i = 0; i < contactCount; ++i) {
    if (samePubKey(contacts[i].pubkey, pubkey)) {
      idx = i;
      break;
    }
  }

  if (idx < 0) {
    if (contactCount >= MAX_CONTACTS) return;
    idx = contactCount++;
  }

  memcpy(contacts[idx].pubkey, pubkey, 32);
  contacts[idx].type = data[33];

  size_t nameLen = 0;
  while (nameLen < 32 && data[100 + nameLen] != 0) nameLen++;
  safeCopyText(contacts[idx].name, sizeof(contacts[idx].name), &data[100], nameLen);

  if (contacts[idx].name[0] == '\0') {
    snprintf(contacts[idx].name, sizeof(contacts[idx].name), "Unnamed");
  }

  contacts[idx].lastAdvert = readLE32(&data[132]);
}

void parseIncomingChannelMessage(const uint8_t* data, size_t len) {
  size_t textOffset = 0;

  if (data[0] == RESP_CHANNEL_MSG && len > 8) {
    textOffset = 8;
  } else if (data[0] == RESP_CHANNEL_MSG_V3 && len > 11) {
    textOffset = 11;
  } else {
    return;
  }

  safeCopyText(lastChatMessage, sizeof(lastChatMessage),
               &data[textOffset], len - textOffset);

  snprintf(chatStatus, sizeof(chatStatus), "MESSAGE RECEIVED");
  messagePollInFlight = false;
  messagePollStartedAt = 0;
  messageWaiting = true; // ask once more; 0x0A ends the queue
  if (notificationsArmed) notificationBeepRequested = true;
  uiDirty = true;
}

void parseIncomingPrivateMessage(const uint8_t* data, size_t len) {
  if (!data || len < 13) return;

  size_t offset = 1;
  int8_t snr4 = 0;
  bool hasSnr = false;

  if (data[0] == RESP_CONTACT_MSG_V3) {
    if (len < 16) return;
    snr4 = (int8_t)data[1];
    hasSnr = true;
    offset = 4;
  }

  if (offset + 12 > len) return;

  const uint8_t* prefix = &data[offset];
  offset += 6;

  uint8_t pathLen = data[offset++];
  (void)pathLen;
  uint8_t txtType = data[offset++];

  if (offset + 4 > len) return;
  uint32_t timestamp = readLE32(&data[offset]);
  offset += 4;

  if (txtType == 2) {
    if (offset + 4 > len) return;
    offset += 4;
  }

  if (offset > len) return;

  char msg[80];
  safeCopyText(msg, sizeof(msg), &data[offset], len - offset);
  addPrivateMessage(prefix, false, hasSnr ? snr4 : 0, timestamp, msg);

  int cidx = findContactByPrefix(prefix, 6);
  if (cidx >= 0) {
    if (!(currentScreen == SCREEN_PRIVATE_CHAT && privateContactIndex == cidx)) {
      privateUnread = true;
      memcpy(privateUnreadPrefix, prefix, 6);
    }
  }

  messagePollInFlight = false;
  messagePollStartedAt = 0;
  messageWaiting = true;
  if (notificationsArmed) notificationBeepRequested = true;

  if (currentScreen == SCREEN_PRIVATE_CHAT && cidx == privateContactIndex) {
    if (hasSnr) {
      snprintf(privateStatus, sizeof(privateStatus), "RX SNR %+.1f dB", ((float)snr4) / 4.0f);
    } else {
      snprintf(privateStatus, sizeof(privateStatus), "MESSAGE RECEIVED");
    }
  }

  uiDirty = true;
}

void handleFrame(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;

  const uint8_t code = data[0];

  switch (code) {
    case RESP_OK:
      if (saveStage == SAVE_WAIT_ACK) { saveAck = 1; break; }
      if (advertPending) {
        advertPending = false;
        advertSuccessUntil = millis() + 1800;
        snprintf(advertStatus, sizeof(advertStatus), "ADVERT SENT");
      } else if (chatPending && chatPendingKind == CHAT_PENDING_PUBLIC) {
        chatPending = false;
        chatPendingKind = CHAT_PENDING_NONE;
        chatStartedAt = 0;
        snprintf(chatStatus, sizeof(chatStatus), "MESSAGE SENT");
      } else if (repeaterScanning) {
        snprintf(repeaterScanStatus, sizeof(repeaterScanStatus), "LISTENING FOR REPEATERS...");
      }
      uiDirty = true;
      break;

    case RESP_ERR:
      if (saveStage == SAVE_WAIT_ACK) { saveAck = -1; break; }
      if (advertPending) {
        advertPending = false;
        snprintf(advertStatus, sizeof(advertStatus), "ADVERT ERROR");
      }
      if (pingPending) {
        pingPending = false;
        snprintf(pingStatus, sizeof(pingStatus), "PING ERROR");
      }
      if (chatPending) {
        chatPending = false;
        chatStartedAt = 0;
        if (chatPendingKind == CHAT_PENDING_PRIVATE) {
          snprintf(privateStatus, sizeof(privateStatus), "SEND ERROR");
        } else {
          snprintf(chatStatus, sizeof(chatStatus), "SEND ERROR");
        }
        chatPendingKind = CHAT_PENDING_NONE;
      }
      if (repeaterScanning) {
        repeaterScanning = false;
        snprintf(repeaterScanStatus, sizeof(repeaterScanStatus), "DISCOVERY ERROR / UNSUPPORTED");
      }
      messagePollInFlight = false;
      messagePollStartedAt = 0;
      uiDirty = true;
      break;

    case RESP_CONTACTS_START:
      contactCount = 0;
      contactsLoaded = false;
      uiDirty = true;
      break;

    case RESP_CONTACT:
      parseContactFrame(data, len);
      break;

    case RESP_END_CONTACTS:
      contactsLoaded = true;

      if (protocolStage == PROTO_SYNC_CONTACTS_INIT) {
        protocolStage = PROTO_NEED_TIME;
      } else {
        protocolStage = PROTO_READY;
      }

      protocolStageStarted = millis();
      uiDirty = true;
      break;

    case RESP_SELF_INFO:
      parseSelfInfo(data, len);
      break;

    case RESP_DEFAULT_FLOOD_SCOPE:
      if(regionQueryAwaiting && (len==1 || len>=48)) {
        if(len==1) radioRegionName[0]='\0';
        else safeCopyText(radioRegionName,sizeof(radioRegionName),&data[1],31);
        regionKnown=true;regionQueryAwaiting=false;uiDirty=true;
      }
      break;

    case RESP_SENT:
      if (pingPending && len >= 10) {
        // MeshCore sends an estimated timeout at bytes 6..9.
        uint32_t suggested = readLE32(&data[6]);
        if (suggested < 3000) suggested = 3000;
        if (suggested > 15000) suggested = 15000;
        pingTimeoutMs = suggested + 1200;
        snprintf(pingStatus, sizeof(pingStatus), "SENT - WAITING REPLY");
      } else if (chatPending && chatPendingKind == CHAT_PENDING_PRIVATE && len >= 10) {
        uint32_t suggested = readLE32(&data[6]);
        privateExpectedAck = readLE32(&data[2]);
        privateAckWaiting = privateExpectedAck != 0;
        privateAckStartedAt = millis();
        privateAckTimeoutMs = suggested + 1800;
        if (privateAckTimeoutMs < 4000) privateAckTimeoutMs = 4000;
        if (privateAckTimeoutMs > 20000) privateAckTimeoutMs = 20000;

        snprintf(privateStatus, sizeof(privateStatus),
                 data[1] ? "SENT FLOOD - WAIT ACK" : "SENT DIRECT - WAIT ACK");
        chatPending = false;
        chatPendingKind = CHAT_PENDING_NONE;
        chatStartedAt = 0;
      }
      uiDirty = true;
      break;

    case RESP_CURRENT_TIME:
      if (len >= 5) {
        deviceEpochUTC = readLE32(&data[1]);
        deviceEpochMillis = millis();
      }
      protocolStage = PROTO_READY;
      protocolStageStarted = millis();
      uiDirty = true;
      break;

    case RESP_CONTACT_MSG:
    case RESP_CONTACT_MSG_V3:
      parseIncomingPrivateMessage(data, len);
      break;

    case RESP_CHANNEL_MSG:
    case RESP_CHANNEL_MSG_V3:
      parseIncomingChannelMessage(data, len);
      break;

    case RESP_NO_MORE_MESSAGES:
      // Once the initial backlog is empty, future received messages may beep.
      notificationsArmed = true;
      messagePollInFlight = false;
      messagePollStartedAt = 0;
      messageWaiting = false;
      uiDirty = true;
      break;

    case PUSH_ADVERT:
      // 0x80 carries the 32-byte public key of the node just heard.
      // Record RF activity, but DO NOT start a full contacts sync here.
      // On a busy mesh that old behaviour could keep protocolStage away
      // from PROTO_READY and make every user action report BUSY.
      if (len >= 33) noteHeardNode(&data[1]);
      uiDirty = true;
      break;

    case PUSH_SEND_CONFIRMED:
      if (len >= 9 && privateAckWaiting) {
        uint32_t ack = readLE32(&data[1]);
        uint32_t rtt = readLE32(&data[5]);
        if (ack == privateExpectedAck) {
          privateAckWaiting = false;
          snprintf(privateStatus, sizeof(privateStatus), "ACK %lu ms", (unsigned long)rtt);
          uiDirty = true;
        }
      }
      break;

    case PUSH_MESSAGE_WAITING:
      messageWaiting = true;
      break;

    case PUSH_NEW_ADVERT:
      // Same contact-style frame layout on current MeshCore firmware.
      if (len >= 33) noteHeardNode(&data[1]);
      parseContactFrame(data, len);
      contactsLoaded = true;
      uiDirty = true;
      break;

    case PUSH_CONTROL_DATA:
      // v8+ live zero-hop discovery. For DISCOVER_RESP, this gives us:
      // data[1] = response SNR at our node, data[2] = RSSI at our node,
      // payload[1] = SNR of our request as measured by the repeater.
      if (len >= 42 && data[3] == 0) {
        const uint8_t* payload = &data[4];
        uint8_t controlType = payload[0] & 0xF0;
        uint8_t nodeType = payload[0] & 0x0F;
        uint32_t tag = readLE32(&payload[2]);

        if (controlType == CONTROL_DISCOVER_RESP &&
            nodeType == ADV_TYPE_REPEATER &&
            tag == repeaterDiscoverTag) {
          const uint8_t* pubkey = &payload[6];
          int idx = findLiveRepeaterIndex(pubkey);
          if (idx < 0) {
            if (liveRepeaterCount < MAX_LIVE_REPEATERS) {
              idx = liveRepeaterCount++;
            } else {
              idx = 0;
              for (int i = 1; i < MAX_LIVE_REPEATERS; ++i) {
                if (liveRepeaters[i].lastSeenMs < liveRepeaters[idx].lastSeenMs) idx = i;
              }
            }
          }

          memcpy(liveRepeaters[idx].pubkey, pubkey, 32);
          liveRepeaters[idx].rxSnr4 = (int8_t)data[1];
          liveRepeaters[idx].rssi = (int8_t)data[2];
          liveRepeaters[idx].txSnr4 = (int8_t)payload[1];
          liveRepeaters[idx].lastSeenMs = millis();
          noteHeardNode(pubkey);

          snprintf(repeaterScanStatus, sizeof(repeaterScanStatus),
                   "BIDIR RESPONSE - %d LIVE", getLiveRepeaterCount());
          uiDirty = true;
        }
      }
      break;

    case PUSH_PATH_DISCOVERY:
      // Current MeshCore push frame includes target public-key prefix at bytes 2..7.
      if (pingPending && len >= 8 &&
          memcmp(&data[2], pingTargetPrefix, 6) == 0) {
        uint32_t elapsed = millis() - pingStartedAt;

        // A valid path-discovery reply is also positive proof that this
        // repeater is currently reachable from our node.
        for (int i = 0; i < contactCount; ++i) {
          if (contacts[i].type == ADV_TYPE_REPEATER &&
              memcmp(contacts[i].pubkey, pingTargetPrefix, 6) == 0) {
            noteHeardNode(contacts[i].pubkey);
            break;
          }
        }

        pingPending = false;
        snprintf(pingStatus, sizeof(pingStatus), "REPLY IN %lu ms",
                 (unsigned long)elapsed);
        if (currentScreen == SCREEN_LIVE_REPEATERS) {
          snprintf(repeaterScanStatus, sizeof(repeaterScanStatus),
                   "PATH REPLY %lu ms", (unsigned long)elapsed);
        }
        uiDirty = true;
      }
      break;

    default:
      // Keep unknown frames visible in Serial without disturbing the UI.
      break;
  }
}

// ============================================================
// PROTOCOL SERVICE / COMMAND QUEUE
// ============================================================

void requestContactsRefresh() {
  contactsDirty = true;
}

void serviceProtocol() {
  if (!bleConnected || !meshOnline) return;
  if (serviceRadioSettings()) return;
  if(regionQueryAwaiting) {
    if(millis()-regionQueryAt<3500) return; // No infinite BUSY if unsupported.
    regionQueryAwaiting=false;
  }
  if(webActive && regionQueryPending && protocolStage==PROTO_READY &&
     !userCommandBusy() && !messagePollInFlight && !pingPending &&
     !advertPending && !repeaterScanning) {
    uint8_t cmd[]={CMD_GET_DEFAULT_FLOOD_SCOPE};
    if(sendFrame(cmd,sizeof(cmd))) {
      regionQueryPending=false;regionQueryAwaiting=true;regionQueryAt=millis();
      return;
    }
    regionQueryPending=false;
  }

  // APP_START watchdog: if SELF_INFO never arrives, resend once every ~6 s.
  if (protocolStage == PROTO_WAIT_SELF) {
    if (appStartSentAt != 0 && millis() - appStartSentAt > 6000) {
      sendAppStart();
    }
    return;
  }

  // Ping timeout.
  if (pingPending && millis() - pingStartedAt > pingTimeoutMs) {
    pingPending = false;
    snprintf(pingStatus, sizeof(pingStatus), "NO REPLY / TIMEOUT");
    if (currentScreen == SCREEN_LIVE_REPEATERS) {
      snprintf(repeaterScanStatus, sizeof(repeaterScanStatus), "PATH TIMEOUT");
    }
    uiDirty = true;
  }

  // Advert timeout.
  if (advertPending && millis() - advertStartedAt > 6000) {
    advertPending = false;
    snprintf(advertStatus, sizeof(advertStatus), "NO RESPONSE");
    uiDirty = true;
  }

  // Chat timeout: a missed RESP_SENT must never leave chatPending stuck.
  if (chatPending &&
      chatStartedAt != 0 &&
      millis() - chatStartedAt > CHAT_BUSY_TIMEOUT_MS) {
    chatPending = false;
    chatStartedAt = 0;
    if (chatPendingKind == CHAT_PENDING_PRIVATE) {
      snprintf(privateStatus, sizeof(privateStatus), "NO RESPONSE - READY");
    } else {
      snprintf(chatStatus, sizeof(chatStatus), "NO RESPONSE - READY");
    }
    chatPendingKind = CHAT_PENDING_NONE;
    Serial.println("[BUSY] Chat watchdog released");
    uiDirty = true;
  }

  // Private-message ACK timeout.
  if (privateAckWaiting && millis() - privateAckStartedAt > privateAckTimeoutMs) {
    privateAckWaiting = false;
    snprintf(privateStatus, sizeof(privateStatus), "NO ACK / TIMEOUT");
    uiDirty = true;
  }

  // End the live repeater listening window without calling it a failure.
  if (repeaterScanning && millis() > repeaterScanUntil) {
    repeaterScanning = false;
    snprintf(repeaterScanStatus, sizeof(repeaterScanStatus),
             "SCAN DONE - %d LIVE", getLiveRepeaterCount());
    uiDirty = true;
  }

  // Message-poll timeout: this was the main permanent BUSY latch.
  if (messagePollInFlight &&
      messagePollStartedAt != 0 &&
      millis() - messagePollStartedAt > MESSAGE_POLL_TIMEOUT_MS) {
    messagePollInFlight = false;
    messagePollStartedAt = 0;
    messageWaiting = false;
    Serial.println("[BUSY] Message poll watchdog released");
    uiDirty = true;
  }

  // With an empty initial inbox no NO_MORE_MESSAGES frame is produced.
  // Arm after the session is stable, but never while a backlog is draining.
  if (!notificationsArmed && protocolStage == PROTO_READY &&
      !messageWaiting && !messagePollInFlight &&
      millis() - protocolStageStarted >= 3000) {
    notificationsArmed = true;
  }

  switch (protocolStage) {
    case PROTO_NEED_CONTACTS: {
      uint8_t cmd[] = { CMD_GET_CONTACTS };
      if (sendFrame(cmd, sizeof(cmd))) {
        protocolStage = PROTO_SYNC_CONTACTS_INIT;
        protocolStageStarted = millis();
      }
      break;
    }

    case PROTO_SYNC_CONTACTS_INIT:
      if (millis() - protocolStageStarted > 7000) {
        protocolStage = PROTO_NEED_TIME;
      }
      break;

    case PROTO_NEED_TIME: {
      uint8_t cmd[] = { CMD_GET_DEVICE_TIME };
      if (sendFrame(cmd, sizeof(cmd))) {
        protocolStage = PROTO_WAIT_TIME;
        protocolStageStarted = millis();
      }
      break;
    }

    case PROTO_WAIT_TIME:
      if (millis() - protocolStageStarted > 4000) {
        protocolStage = PROTO_READY;
      }
      break;

    case PROTO_SYNC_CONTACTS_USER:
      if (millis() - protocolStageStarted > 7000) {
        protocolStage = PROTO_READY;
      }
      break;

    case PROTO_READY:
      // Explicit commands + an in-flight background poll serialize protocol work.
      // Explicit user actions themselves are NOT blocked by messagePollInFlight.
      if (userCommandBusy() || messagePollInFlight) break;

      if (repeaterScanRequested) {
        repeaterScanRequested = false;
        sendRepeaterDiscovery();
        break;
      }

      if (contactsDirty) {
        contactsDirty = false;
        uint8_t cmd[] = { CMD_GET_CONTACTS };
        if (sendFrame(cmd, sizeof(cmd))) {
          protocolStage = PROTO_SYNC_CONTACTS_USER;
          protocolStageStarted = millis();
        }
        break;
      }

      if (messageWaiting && !messagePollInFlight) {
        uint8_t cmd[] = { CMD_SYNC_NEXT_MESSAGE };
        if (sendFrame(cmd, sizeof(cmd))) {
          messagePollInFlight = true;
          messagePollStartedAt = millis();
        }
      }
      break;

    default:
      break;
  }
}

// ============================================================
// REAL MESHCORE ACTIONS
// ============================================================

int getRepeaterCount() {
  int n = 0;
  for (int i = 0; i < contactCount; ++i) {
    if (contacts[i].type == ADV_TYPE_REPEATER) n++;
  }
  return n;
}

int getRepeaterContactIndex(int repeaterListIndex) {
  int n = 0;
  for (int i = 0; i < contactCount; ++i) {
    if (contacts[i].type == ADV_TYPE_REPEATER) {
      if (n == repeaterListIndex) return i;
      n++;
    }
  }
  return -1;
}

int getChatContactCount() {
  int n = 0;
  for (int i = 0; i < contactCount; ++i) {
    if (contacts[i].type == ADV_TYPE_CHAT) n++;
  }
  return n;
}

int getChatContactIndex(int chatListIndex) {
  int n = 0;
  for (int i = 0; i < contactCount; ++i) {
    if (contacts[i].type == ADV_TYPE_CHAT) {
      if (n == chatListIndex) return i;
      n++;
    }
  }
  return -1;
}

void sendRepeaterDiscovery() {
  if (!meshOnline || protocolStage != PROTO_READY || userCommandBusy()) {
    snprintf(repeaterScanStatus, sizeof(repeaterScanStatus), "BUSY - TRY AGAIN");
    uiDirty = true;
    return;
  }

  // Repeaters rate-limit discovery. Avoid hammering them if R is pressed repeatedly.
  if (lastRepeaterScanAt != 0 && millis() - lastRepeaterScanAt < 30000UL) {
    uint32_t waitSec = (30000UL - (millis() - lastRepeaterScanAt) + 999) / 1000;
    snprintf(repeaterScanStatus, sizeof(repeaterScanStatus), "SCAN COOLDOWN %lus", (unsigned long)waitSec);
    uiDirty = true;
    return;
  }

  repeaterDiscoverTag = (uint32_t)micros() ^ ((uint32_t)millis() << 11) ^ 0xA51926UL;
  if (repeaterDiscoverTag == 0) repeaterDiscoverTag = 1;

  uint8_t frame[11];
  frame[0] = CMD_SEND_CONTROL_DATA;
  frame[1] = CONTROL_DISCOVER_REQ;              // full 32-byte pubkey response
  frame[2] = (1 << ADV_TYPE_REPEATER);          // repeater filter only
  writeLE32(&frame[3], repeaterDiscoverTag);
  writeLE32(&frame[7], 0);                      // since = 0

  if (sendFrame(frame, sizeof(frame))) {
    repeaterScanning = true;
    repeaterScanUntil = millis() + 6500UL;
    lastRepeaterScanAt = millis();
    snprintf(repeaterScanStatus, sizeof(repeaterScanStatus), "SCANNING ZERO-HOP...");
  } else {
    snprintf(repeaterScanStatus, sizeof(repeaterScanStatus), "SCAN SEND FAILED");
  }

  uiDirty = true;
}

void sendPathDiscoveryToContact(int contactIndex, const char* displayName) {
  if (!meshOnline || protocolStage != PROTO_READY || userCommandBusy()) {
    snprintf(pingStatus, sizeof(pingStatus), "BUSY - TRY AGAIN");
    uiDirty = true;
    return;
  }

  if (contactIndex < 0 || contactIndex >= contactCount ||
      contacts[contactIndex].type != ADV_TYPE_REPEATER) {
    snprintf(pingStatus, sizeof(pingStatus), "INVALID REPEATER");
    uiDirty = true;
    return;
  }

  uint8_t frame[34];
  frame[0] = CMD_SEND_PATH_DISCOVERY_REQ;
  frame[1] = 0; // required reserved byte
  memcpy(&frame[2], contacts[contactIndex].pubkey, 32);

  memcpy(pingTargetPrefix, contacts[contactIndex].pubkey, 6);
  pingStartedAt = millis();
  pingTimeoutMs = 10000;
  pingPending = true;

  snprintf(pingStatus, sizeof(pingStatus), "PINGING %s...",
           displayName ? displayName : contacts[contactIndex].name);

  if (!sendFrame(frame, sizeof(frame))) {
    pingPending = false;
    snprintf(pingStatus, sizeof(pingStatus), "SEND FAILED");
  }

  uiDirty = true;
}

void sendPingToSelectedRepeater() {
  int total = getRepeaterCount();
  if (total <= 0) {
    snprintf(pingStatus, sizeof(pingStatus), "NO REPEATERS - R REFRESH");
    requestContactsRefresh();
    uiDirty = true;
    return;
  }

  if (pingSelection >= total) pingSelection = total - 1;
  if (pingSelection < 0) pingSelection = 0;

  int contactIndex = getRepeaterContactIndex(pingSelection);
  if (contactIndex < 0) return;

  sendPathDiscoveryToContact(contactIndex, contacts[contactIndex].name);
}

void sendPathToSelectedLiveRepeater() {
  int total = getLiveRepeaterCount();
  if (total <= 0) {
    snprintf(repeaterScanStatus, sizeof(repeaterScanStatus), "NO LIVE REPEATERS - R SCAN");
    repeaterScanRequested = true;
    uiDirty = true;
    return;
  }

  if (liveRepeaterSelection >= total) liveRepeaterSelection = total - 1;
  if (liveRepeaterSelection < 0) liveRepeaterSelection = 0;

  int contactIndex = getLiveRepeaterContactIndex(liveRepeaterSelection);
  if (contactIndex < 0) return;

  sendPathDiscoveryToContact(contactIndex, contacts[contactIndex].name);
  if (pingPending) {
    snprintf(repeaterScanStatus, sizeof(repeaterScanStatus), "PATH %s...", contacts[contactIndex].name);
  }
}

void sendAdvert(bool flood) {
  if (!meshOnline || protocolStage != PROTO_READY || userCommandBusy()) {
    snprintf(advertStatus, sizeof(advertStatus), "BUSY - TRY AGAIN");
    uiDirty = true;
    return;
  }

  uint8_t frame[2] = {
    CMD_SEND_SELF_ADVERT,
    flood ? (uint8_t)1 : (uint8_t)0
  };

  advertPending = true;
  advertStartedAt = millis();
  advertSuccessUntil = 0;
  advertAnimPhase = 0;

  snprintf(advertStatus, sizeof(advertStatus),
           flood ? "SENDING FLOOD..." : "SENDING ZERO-HOP...");

  if (!sendFrame(frame, sizeof(frame))) {
    advertPending = false;
    snprintf(advertStatus, sizeof(advertStatus), "SEND FAILED");
  }

  uiDirty = true;
}

void sendChatMessage() {
  if (chatInput.length() == 0) return;

  if (!meshOnline || protocolStage != PROTO_READY || userCommandBusy()) {
    snprintf(chatStatus, sizeof(chatStatus), "BUSY - TRY AGAIN");
    uiDirty = true;
    return;
  }

  uint32_t epoch = currentDeviceEpochUTC();
  if (epoch == 0) {
    snprintf(chatStatus, sizeof(chatStatus), "WAITING FOR DEVICE TIME");
    uiDirty = true;
    return;
  }

  // Command:
  // [0] CMD_SEND_CHANNEL_MSG
  // [1] 0x00 reserved
  // [2] channel index (0)
  // [3..6] timestamp LE
  // [7..] UTF-8 text
  size_t textLen = chatInput.length();
  if (textLen > 120) textLen = 120;

  uint8_t frame[128];
  frame[0] = CMD_SEND_CHANNEL_MSG;
  frame[1] = 0x00;
  frame[2] = 0; // public channel 0
  writeLE32(&frame[3], epoch);
  memcpy(&frame[7], chatInput.c_str(), textLen);

  char sentPreview[96];
  snprintf(sentPreview, sizeof(sentPreview), "YOU: %.88s", chatInput.c_str());
  strncpy(lastChatMessage, sentPreview, sizeof(lastChatMessage) - 1);
  lastChatMessage[sizeof(lastChatMessage) - 1] = '\0';

  chatPending = true;
  chatPendingKind = CHAT_PENDING_PUBLIC;
  chatStartedAt = millis();
  snprintf(chatStatus, sizeof(chatStatus), "SENDING...");

  if (sendFrame(frame, 7 + textLen)) {
    chatInput = "";
  } else {
    chatPending = false;
    chatPendingKind = CHAT_PENDING_NONE;
    chatStartedAt = 0;
    snprintf(chatStatus, sizeof(chatStatus), "SEND FAILED");
  }

  uiDirty = true;
}

void openPrivateChat(int contactIndex) {
  if (contactIndex < 0 || contactIndex >= contactCount) return;
  if (contacts[contactIndex].type != ADV_TYPE_CHAT) return;

  privateReturnScreen = currentScreen;
  privateContactIndex = contactIndex;
  privateInput = "";
  snprintf(privateStatus, sizeof(privateStatus), "READY");

  if (privateUnread && memcmp(privateUnreadPrefix, contacts[contactIndex].pubkey, 6) == 0) {
    privateUnread = false;
  }

  setScreen(SCREEN_PRIVATE_CHAT);
}

void sendPrivateMessage() {
  if (privateContactIndex < 0 || privateContactIndex >= contactCount) return;
  if (privateInput.length() == 0) return;

  if (privateAckWaiting) {
    snprintf(privateStatus, sizeof(privateStatus), "WAITING FOR PREVIOUS ACK");
    uiDirty = true;
    return;
  }

  if (!meshOnline || protocolStage != PROTO_READY || userCommandBusy()) {
    snprintf(privateStatus, sizeof(privateStatus), "BUSY - TRY AGAIN");
    uiDirty = true;
    return;
  }

  uint32_t epoch = currentDeviceEpochUTC();
  if (epoch == 0) {
    snprintf(privateStatus, sizeof(privateStatus), "WAITING FOR DEVICE TIME");
    uiDirty = true;
    return;
  }

  size_t textLen = privateInput.length();
  if (textLen > 120) textLen = 120;

  uint8_t frame[140];
  frame[0] = CMD_SEND_TXT_MSG;
  frame[1] = 0;   // TXT_TYPE_PLAIN
  frame[2] = 0;   // attempt 0
  writeLE32(&frame[3], epoch);
  memcpy(&frame[7], contacts[privateContactIndex].pubkey, 6);
  memcpy(&frame[13], privateInput.c_str(), textLen);

  char sentText[80];
  safeCopyText(sentText, sizeof(sentText), (const uint8_t*)privateInput.c_str(), textLen);

  chatPending = true;
  chatPendingKind = CHAT_PENDING_PRIVATE;
  chatStartedAt = millis();
  privateAckWaiting = false;
  snprintf(privateStatus, sizeof(privateStatus), "SENDING...");

  if (sendFrame(frame, 13 + textLen)) {
    addPrivateMessage(contacts[privateContactIndex].pubkey, true, 0, epoch, sentText);
    privateInput = "";
  } else {
    chatPending = false;
    chatPendingKind = CHAT_PENDING_NONE;
    chatStartedAt = 0;
    snprintf(privateStatus, sizeof(privateStatus), "SEND FAILED");
  }

  uiDirty = true;
}

// ============================================================
// UI DRAWING
// ============================================================

void setBlueTheme() {
  C_BG             = D.color565(18, 20, 23);
  C_PANEL          = D.color565(25, 29, 34);
  C_PANEL_SELECTED = D.color565(5, 47, 78);
  C_BLUE           = D.color565(0, 168, 255);
  C_BLUE_DIM       = D.color565(0, 88, 135);
  C_HEADER         = D.color565(222, 224, 226);
  C_WHITE          = D.color565(245, 247, 249);
  C_MUTED          = D.color565(120, 132, 140);
}

void centerText(const char* text, int y, float size, uint16_t color) {
  D.setTextSize(size);
  D.setTextColor(color, C_BG);
  int w = D.textWidth(text);
  D.setCursor((D.width() - w) / 2, y);
  D.print(text);
}

void showBootScreen() {
  D.fillScreen(C_BG);

  // NodePad publication splash.
  // NodePad / OFF-GRID MESH TERMINAL / for M5Stack Cardputer / V2.2.2
  D.setTextSize(3);
  D.setTextColor(C_BLUE, C_BG);
  int logoW = D.textWidth("NodePad");
  D.setCursor((D.width() - logoW) / 2, 24);
  D.print("NodePad");

  centerText("OFF-GRID MESH TERMINAL", 65, 1.25, C_WHITE);
  centerText("for M5Stack Cardputer", 94, 1.20, C_WHITE);
  centerText(APP_VERSION, 113, 1.20, C_WHITE);

  // Push the complete splash as one frame: no flicker.
  D.pushSprite(0, 0);

  delay(5000);
}

void drawHeader(const char* title) {
  D.fillRect(0, 0, 240, 19, C_HEADER);

  D.setTextSize(1);
  D.setTextColor(C_BLUE, C_HEADER);
  D.setCursor(5, 6);
  D.print(title);

  // Battery is the only item on the right side of the header.
  // No status circle: link state remains visible in the footer/System page.
  char batteryText[8];
  if (batteryPercent >= 0) {
    snprintf(batteryText, sizeof(batteryText), "%d%%", batteryPercent);
  } else {
    snprintf(batteryText, sizeof(batteryText), "--%%");
  }

  int batteryW = D.textWidth(batteryText);
  D.setCursor(235 - batteryW, 6);
  D.print(batteryText);
}

void drawFooter() {
  D.drawFastHLine(4, 116, 232, C_BLUE_DIM);

  D.setTextSize(1);
  D.setCursor(5, 122);

  if (bleSecure) {
    D.setTextColor(C_BLUE, C_BG);
    D.print("BLE SECURE");
  } else if (bleConnected) {
    D.setTextColor(C_BLUE_DIM, C_BG);
    D.print("BLE");
  } else {
    D.setTextColor(C_MUTED, C_BG);
    D.print("BLE OFF");
  }

  D.setTextColor(C_BLUE_DIM, C_BG);
  D.print(" | ");

  if (meshOnline) {
    D.setTextColor(C_BLUE, C_BG);
    D.print("MESH ONLINE");
  } else {
    D.setTextColor(C_MUTED, C_BG);
    D.print("MESH OFF");
  }

  // Battery moved to the header. Footer keeps the compact heard-repeater count.
  char rightStatus[12];
  snprintf(rightStatus, sizeof(rightStatus), "R%d", getAttachedRepeaterCount());

  int rightW = D.textWidth(rightStatus);
  D.setTextColor(C_BLUE, C_BG);
  D.setCursor(235 - rightW, 122);
  D.print(rightStatus);
}

void drawSectionTitle(const char* title) {
  D.setTextSize(1.3);
  D.setTextColor(C_BLUE, C_BG);
  D.setCursor(5, 23);
  D.print(title);
  D.drawFastHLine(5, 36, 230, C_BLUE_DIM);
}

void drawMenuRow(int y, const char* label, bool selected) {
  if (selected) {
    D.fillRoundRect(5, y, 230, 15, 3, C_PANEL_SELECTED);
    D.drawRoundRect(5, y, 230, 15, 3, C_BLUE);
  }

  D.setTextSize(1);
  D.setTextColor(selected ? C_WHITE : C_BLUE, selected ? C_PANEL_SELECTED : C_BG);
  D.setCursor(13, y + 4);
  D.print(label);

  D.setCursor(220, y + 4);
  D.print(">");
}

void drawMainScreen() {
  D.fillScreen(C_BG);
  drawHeader("NodePad");
  drawSectionTitle("MAIN MENU");

  static const char* items[] = {
    "CHAT",
    "PRIVATE CHATS",
    "NODES",
    "PING REPEATERS",
    "LIVE REPEATERS",
    "ADVERT",
    "SYSTEM"
  };

  static const int itemCount = 7;
  static const int visibleRows = 5;
  const int rowY[] = {39, 54, 69, 84, 99};

  if (mainSelection < 0) mainSelection = itemCount - 1;
  if (mainSelection >= itemCount) mainSelection = 0;

  int startItem = mainSelection - 2;
  if (startItem < 0) startItem = 0;
  if (startItem > itemCount - visibleRows) startItem = itemCount - visibleRows;

  for (int row = 0; row < visibleRows; ++row) {
    int item = startItem + row;
    drawMenuRow(rowY[row], items[item], item == mainSelection);
  }

  // Small scroll hints: the original blue style stays intact.
  D.setTextSize(1);
  D.setTextColor(C_BLUE_DIM, C_BG);
  if (startItem > 0) D.drawTriangle(2, 43, 4, 39, 6, 43, C_BLUE_DIM);
  if (startItem + visibleRows < itemCount) D.drawTriangle(2, 99, 4, 103, 6, 99, C_BLUE_DIM);

  drawFooter();
}

void drawChatScreen() {
  D.fillScreen(C_BG);
  drawHeader("NodePad / Chat");
  drawSectionTitle("PUBLIC CHANNEL 0");

  D.setTextSize(1);
  D.setTextColor(C_MUTED, C_BG);
  D.setCursor(6, 42);
  D.print("LAST:");

  D.setTextColor(C_BLUE, C_BG);
  String msg = String(lastChatMessage);
  if (msg.length() > 35) {
    D.setCursor(6, 52);
    D.print(msg.substring(0, 35));
    D.setCursor(6, 62);
    D.print(msg.substring(35, min((int)msg.length(), 70)));
  } else {
    D.setCursor(6, 52);
    D.print(msg);
  }

  D.drawFastHLine(5, 79, 230, C_BLUE_DIM);

  D.setTextColor(C_WHITE, C_BG);
  D.setCursor(6, 85);
  D.print("> ");

  String shown = chatInput;
  if (shown.length() > 31) shown = shown.substring(shown.length() - 31);
  D.print(shown);
  D.print("_");

  D.setTextColor(C_MUTED, C_BG);
  D.setCursor(6, 100);
  D.print(chatStatus);

  drawFooter();
}

void drawPrivateListScreen() {
  D.fillScreen(C_BG);
  drawHeader("NodePad / Private Chats");

  int total = getChatContactCount();
  char title[40];
  snprintf(title, sizeof(title), "PRIVATE CHATS  %d", total);
  drawSectionTitle(title);

  if (!contactsLoaded && contactCount == 0) {
    D.setTextSize(1);
    D.setTextColor(C_MUTED, C_BG);
    D.setCursor(10, 51);
    D.print("SYNCING CONTACTS...");
    D.setCursor(10, 67);
    D.print("R = REFRESH");
    drawFooter();
    return;
  }

  if (total <= 0) {
    D.setTextSize(1);
    D.setTextColor(C_MUTED, C_BG);
    D.setCursor(10, 51);
    D.print("NO CHAT CONTACTS");
    D.setCursor(10, 67);
    D.print("R = REFRESH");
    drawFooter();
    return;
  }

  if (privateListSelection >= total) privateListSelection = total - 1;
  if (privateListSelection < 0) privateListSelection = 0;

  int start = privateListSelection - 2;
  if (start < 0) start = 0;
  if (start > total - 5) start = max(0, total - 5);

  for (int row = 0; row < 5; ++row) {
    int listIdx = start + row;
    if (listIdx >= total) break;

    int cidx = getChatContactIndex(listIdx);
    if (cidx < 0) continue;

    int y = 39 + row * 15;
    bool selected = listIdx == privateListSelection;

    if (selected) {
      D.fillRoundRect(5, y, 230, 14, 3, C_PANEL_SELECTED);
      D.drawRoundRect(5, y, 230, 14, 3, C_BLUE);
    }

    uint16_t bg = selected ? C_PANEL_SELECTED : C_BG;
    D.setTextSize(1);
    D.setTextColor(selected ? C_WHITE : C_BLUE, bg);
    D.setCursor(10, y + 3);

    char shortName[27];
    snprintf(shortName, sizeof(shortName), "%.22s", contacts[cidx].name);
    D.print(shortName);

    bool unread = privateUnread &&
                  memcmp(privateUnreadPrefix, contacts[cidx].pubkey, 6) == 0;
    D.setCursor(207, y + 3);
    D.print(unread ? "NEW" : ">");
  }

  D.setTextSize(1);
  D.setTextColor(C_MUTED, C_BG);
  D.setCursor(6, 106);
  D.print("ENTER OPEN CHAT   R REFRESH");

  drawFooter();
}

void drawNodesScreen() {
  D.fillScreen(C_BG);
  drawHeader("NodePad / Nodes");

  char title[40];
  snprintf(title, sizeof(title), "NODES  %d", contactCount);
  drawSectionTitle(title);

  if (!contactsLoaded && contactCount == 0) {
    D.setTextSize(1);
    D.setTextColor(C_MUTED, C_BG);
    D.setCursor(10, 51);
    D.print("SYNCING CONTACTS...");
    D.setCursor(10, 67);
    D.print("R = REFRESH");
    drawFooter();
    return;
  }

  int total = contactCount;
  if (total <= 0) {
    D.setTextSize(1);
    D.setTextColor(C_MUTED, C_BG);
    D.setCursor(10, 51);
    D.print("NO CONTACTS");
    D.setCursor(10, 67);
    D.print("R = REFRESH");
    drawFooter();
    return;
  }

  if (nodesSelection >= total) nodesSelection = total - 1;
  if (nodesSelection < 0) nodesSelection = 0;

  int start = nodesSelection - 2;
  if (start < 0) start = 0;
  if (start > total - 5) start = max(0, total - 5);

  for (int row = 0; row < 5; ++row) {
    int idx = start + row;
    if (idx >= total) break;

    int y = 39 + row * 15;
    bool selected = idx == nodesSelection;

    if (selected) {
      D.fillRoundRect(5, y, 230, 14, 3, C_PANEL_SELECTED);
      D.drawRoundRect(5, y, 230, 14, 3, C_BLUE);
    }

    D.setTextSize(1);
    D.setTextColor(selected ? C_WHITE : C_BLUE,
                   selected ? C_PANEL_SELECTED : C_BG);
    D.setCursor(10, y + 3);

    char shortName[23];
    snprintf(shortName, sizeof(shortName), "%.18s", contacts[idx].name);
    D.print(shortName);

    D.setCursor(195, y + 3);
    if (contacts[idx].type == ADV_TYPE_CHAT &&
        privateUnread && memcmp(privateUnreadPrefix, contacts[idx].pubkey, 6) == 0) {
      D.print("CHAT*");
    } else {
      D.print(typeName(contacts[idx].type));
    }
  }

  D.setTextSize(1);
  D.setTextColor(C_MUTED, C_BG);
  D.setCursor(6, 106);
  if (nodesSelection >= 0 && nodesSelection < contactCount &&
      contacts[nodesSelection].type == ADV_TYPE_CHAT) {
    D.print("ENTER PRIVATE CHAT   R REFRESH");
  } else {
    D.print("R REFRESH");
  }

  drawFooter();
}

void drawPrivateChatScreen() {
  D.fillScreen(C_BG);

  char head[42];
  if (privateContactIndex >= 0 && privateContactIndex < contactCount) {
    snprintf(head, sizeof(head), "Private / %.24s", contacts[privateContactIndex].name);
  } else {
    snprintf(head, sizeof(head), "Private Chat");
  }
  drawHeader(head);
  drawSectionTitle("DIRECT MESSAGE");

  if (privateContactIndex < 0 || privateContactIndex >= contactCount) {
    D.setTextColor(C_MUTED, C_BG);
    D.setCursor(8, 52);
    D.print("NO CONTACT SELECTED");
    drawFooter();
    return;
  }

  const uint8_t* prefix = contacts[privateContactIndex].pubkey;
  int matches[3] = {-1, -1, -1};
  int found = 0;
  for (int i = privateMessageCount - 1; i >= 0 && found < 3; --i) {
    if (memcmp(privateMessages[i].peerPrefix, prefix, 6) == 0) {
      matches[2 - found] = i;
      found++;
    }
  }

  int y = 41;
  for (int slot = 0; slot < 3; ++slot) {
    int mi = matches[slot];
    if (mi < 0) continue;

    PrivateMessage& m = privateMessages[mi];
    D.setTextSize(1);
    D.setTextColor(m.outgoing ? C_WHITE : C_BLUE, C_BG);
    D.setCursor(6, y);
    D.print(m.outgoing ? "> " : "< ");

    String t = String(m.text);
    if (t.length() > 30) t = t.substring(0, 30);
    D.print(t);

    if (!m.outgoing && m.snr4 != 0) {
      D.setTextColor(C_MUTED, C_BG);
      D.setCursor(190, y);
      D.printf("%+.1f", ((float)m.snr4) / 4.0f);
    }
    y += 15;
  }

  D.drawFastHLine(5, 87, 230, C_BLUE_DIM);
  D.setTextColor(C_WHITE, C_BG);
  D.setCursor(6, 93);
  D.print("> ");
  String shown = privateInput;
  if (shown.length() > 31) shown = shown.substring(shown.length() - 31);
  D.print(shown);
  D.print("_");

  D.setTextColor(privateAckWaiting ? C_WHITE : C_MUTED, C_BG);
  D.setCursor(6, 106);
  D.print(privateStatus);

  drawFooter();
}

void drawPingScreen() {
  D.fillScreen(C_BG);
  drawHeader("NodePad / Ping");

  char title[40];
  snprintf(title, sizeof(title), "REPEATERS  %d", getRepeaterCount());
  drawSectionTitle(title);

  int total = getRepeaterCount();

  if (total <= 0) {
    D.setTextSize(1);
    D.setTextColor(C_MUTED, C_BG);
    D.setCursor(10, 48);
    D.print("NO REPEATERS IN CONTACTS");
    D.setCursor(10, 62);
    D.print("PRESS R TO REFRESH");
  } else {
    if (pingSelection >= total) pingSelection = total - 1;
    if (pingSelection < 0) pingSelection = 0;

    int start = pingSelection - 1;
    if (start < 0) start = 0;
    if (start > total - 4) start = max(0, total - 4);

    for (int row = 0; row < 4; ++row) {
      int listIdx = start + row;
      if (listIdx >= total) break;

      int cidx = getRepeaterContactIndex(listIdx);
      if (cidx < 0) continue;

      int y = 39 + row * 15;
      bool selected = listIdx == pingSelection;

      if (selected) {
        D.fillRoundRect(5, y, 230, 14, 3, C_PANEL_SELECTED);
        D.drawRoundRect(5, y, 230, 14, 3, C_BLUE);
      }

      uint16_t bg = selected ? C_PANEL_SELECTED : C_BG;
      D.setTextSize(1);
      D.setTextColor(selected ? C_WHITE : C_BLUE, bg);
      D.setCursor(10, y + 3);

      char nm[27];
      snprintf(nm, sizeof(nm), "%.21s", contacts[cidx].name);
      D.print(nm);

      D.setCursor(204, y + 3);
      D.print(isRepeaterAttachedByContactIndex(cidx) ? "LIVE" : "PING");
    }
  }

  D.drawFastHLine(5, 100, 230, C_BLUE_DIM);
  D.setTextSize(1);
  D.setTextColor(pingPending ? C_WHITE : C_BLUE, C_BG);
  D.setCursor(6, 105);
  D.print(pingStatus);

  drawFooter();
}

void drawLiveRepeatersScreen() {
  D.fillScreen(C_BG);
  drawHeader("NodePad / Live Repeaters");

  int total = getLiveRepeaterCount();
  int bidir = 0;
  for (int i = 0; i < contactCount; ++i) {
    if (isRepeaterBidirByContactIndex(i)) bidir++;
  }

  char title[48];
  snprintf(title, sizeof(title), "LIVE REPEATERS  %d  BIDIR %d", total, bidir);
  drawSectionTitle(title);

  if (total <= 0) {
    D.setTextSize(1);
    D.setTextColor(C_MUTED, C_BG);
    D.setCursor(9, 49);
    D.print(repeaterScanning ? "SCANNING FOR ZERO-HOP REPEATERS..." : "NO REPEATER HEARD NOW");
    D.setCursor(9, 65);
    D.print("PRESS R FOR LIVE DISCOVERY");
    D.setCursor(9, 81);
    D.print("BIDIR = BOTH DIRECTIONS WORK");
  } else {
    if (liveRepeaterSelection >= total) liveRepeaterSelection = total - 1;
    if (liveRepeaterSelection < 0) liveRepeaterSelection = 0;

    int start = liveRepeaterSelection - 1;
    if (start < 0) start = 0;
    if (start > total - 3) start = max(0, total - 3);

    for (int row = 0; row < 3; ++row) {
      int listIdx = start + row;
      if (listIdx >= total) break;

      int cidx = getLiveRepeaterContactIndex(listIdx);
      if (cidx < 0) continue;

      int y = 39 + row * 21;
      bool selected = listIdx == liveRepeaterSelection;

      if (selected) {
        D.fillRoundRect(5, y, 230, 20, 3, C_PANEL_SELECTED);
        D.drawRoundRect(5, y, 230, 20, 3, C_BLUE);
      }

      uint16_t bg = selected ? C_PANEL_SELECTED : C_BG;
      D.setTextSize(1);
      D.setTextColor(selected ? C_WHITE : C_BLUE, bg);
      D.setCursor(9, y + 2);

      char nm[22];
      snprintf(nm, sizeof(nm), "%.17s", contacts[cidx].name);
      D.print(nm);

      bool bidirNow = isRepeaterBidirByContactIndex(cidx);
      D.setCursor(198, y + 2);
      D.print(bidirNow ? "BIDIR" : "HEARD");

      D.setTextColor(selected ? C_WHITE : C_MUTED, bg);
      D.setCursor(9, y + 11);

      int li = findLiveRepeaterIndex(contacts[cidx].pubkey);
      if (bidirNow && li >= 0) {
        D.printf("RX %+.1fdB %ddBm  TX %+.1fdB",
                 ((float)liveRepeaters[li].rxSnr4) / 4.0f,
                 (int)liveRepeaters[li].rssi,
                 ((float)liveRepeaters[li].txSnr4) / 4.0f);
      } else {
        D.print("advert heard - run R scan for link dB");
      }
    }
  }

  D.setTextSize(1);
  D.setTextColor(repeaterScanning ? C_WHITE : C_BLUE, C_BG);
  D.setCursor(6, 103);
  D.print(repeaterScanStatus);

  D.setTextColor(C_MUTED, C_BG);
  D.setCursor(145, 103);
  D.print("R SCAN / ENTER PATH");

  drawFooter();
}

void drawAdvertScreen() {
  D.fillScreen(C_BG);
  drawHeader("NodePad / Advert");
  drawSectionTitle("SEND SELF ADVERT");

  drawMenuRow(45, "ZERO-HOP ADVERT", advertSelection == 0);
  drawMenuRow(64, "FLOOD ADVERT", advertSelection == 1);

  D.setTextSize(1);
  D.setTextColor(C_MUTED, C_BG);
  D.setCursor(8, 87);
  D.print("ZERO = LOCAL   FLOOD = MESH");

  D.setTextColor(advertPending ? C_WHITE : C_BLUE, C_BG);
  D.setCursor(8, 102);
  D.print(advertStatus);

  // TX animation: expanding radio rings while sending; check mark on success.
  const int cx = 211;
  const int cy = 96;
  if (advertPending) {
    D.fillCircle(cx, cy, 2, C_BLUE);
    for (int r = 0; r < 3; ++r) {
      int radius = 5 + ((advertAnimPhase + r) % 3) * 4;
      D.drawCircle(cx, cy, radius, r == 0 ? C_BLUE : C_BLUE_DIM);
    }
  } else if (millis() < advertSuccessUntil) {
    D.drawCircle(cx, cy, 9, C_BLUE);
    D.drawLine(cx - 4, cy, cx - 1, cy + 4, C_BLUE);
    D.drawLine(cx - 1, cy + 4, cx + 5, cy - 4, C_BLUE);
  }

  drawFooter();
}

void drawSystemScreen() {
  D.fillScreen(C_BG);
  drawHeader("NodePad / System");
  drawSectionTitle("SYSTEM");

  D.setTextSize(1);

  D.setTextColor(C_MUTED, C_BG);
  D.setCursor(7, 38);  D.print("NODE");
  D.setCursor(7, 49);  D.print("RADIO");
  D.setCursor(7, 60);  D.print("LINK");
  D.setCursor(7, 71);  D.print("CONTACTS");
  D.setCursor(7, 82);  D.print("VERSION");
  D.setCursor(7, 93);  D.print("SOUND");

  D.setTextColor(C_BLUE, C_BG);
  D.setCursor(66, 38); D.print(meshNodeName);

  D.setCursor(66, 49);
  if (radioFreqMHz > 0.0f) {
    D.printf("%.3f SF%u CR%u", radioFreqMHz, radioSF, radioCR);
  } else {
    D.print("---");
  }

  D.setCursor(66, 60);
  if (bleSecure && meshOnline) D.print("BLE SECURE / ONLINE");
  else if (bleConnected)      D.print("BLE / STARTING");
  else                        D.print("OFFLINE");

  D.setCursor(66, 71);
  D.printf("%d / RPT %d", contactCount, getAttachedRepeaterCount());

  D.setCursor(66, 82);
  D.print(APP_VERSION);
  D.setCursor(66, 93);
  D.print(notificationSoundLevel == 0 ? "MUTE" : (notificationSoundLevel == 1 ? "LOW" : "MEDIUM"));

  D.setTextColor(C_MUTED, C_BG);
  D.setCursor(7, 105);
  D.print("ENTER: BLE | M: SOUND");
  D.setCursor(7, 116);
  D.print("P: WEB SETUP | C: CREDITS");

  drawFooter();
}


void drawRadioSetupScreen() {
  D.fillScreen(C_BG);
  drawHeader("NodePad / Setup");
  drawSectionTitle("LOCAL WI-FI SETUP");
  D.setTextColor(C_WHITE,C_BG);
  D.setCursor(7,43);D.print("SSID: ");D.print(webSsid);
  D.setCursor(7,59);D.print("PASS: ");D.print(webPassword);
  D.setTextColor(C_BLUE,C_BG);
  D.setCursor(7,77);D.print("http://192.168.4.1");
  D.setTextColor(C_MUTED,C_BG);
  D.setCursor(7,99);D.print(stageLabel());
  D.setCursor(7,115);D.print("ESC = STOP WI-FI & BACK");
  drawFooter();
}
void drawCreditsScreen() {
  D.fillScreen(C_BG);
  drawHeader("NodePad / Credits");
  drawSectionTitle("CREDITS");
  D.setTextColor(C_WHITE,C_BG);
  D.setCursor(7,41);D.print("PROJECT: AL83M");
  D.setTextColor(C_MUTED,C_BG);
  D.setCursor(7,57);D.print("Community beta testers");
  D.setCursor(7,73);D.print("MeshCore / M5Stack");
  D.setCursor(7,89);D.print("NimBLE-Arduino");
  D.setCursor(7,114);D.print("ESC = SYSTEM");
  drawFooter();
}

void drawBleDevicesScreen() {
  D.fillScreen(C_BG);
  drawHeader("NodePad / Bluetooth");
  drawSectionTitle("SELECT COMPANION");

  if (bleChoiceCount == 0) {
    D.setTextSize(1);
    D.setTextColor(C_MUTED, C_BG);
    D.setCursor(8, 52);
    if (bleScanInProgress || bleScanRequested) D.print("Searching for Companions...");
    else D.print("No MeshCore devices found");
  } else {
    const int visibleRows = 4;
    int start = bleListSelection - 1;
    if (start < 0) start = 0;
    if (start > bleChoiceCount - visibleRows)
      start = max(0, bleChoiceCount - visibleRows);
    for (int row = 0; row < visibleRows && start + row < bleChoiceCount; ++row) {
      int i = start + row;
      String label = bleChoices[i].name;
      if (label.length() > 20) label = label.substring(0, 20);
      label += " " + String(bleChoices[i].rssi);
      drawMenuRow(39 + row * 17, label.c_str(), i == bleListSelection);
    }
  }

  D.setTextSize(1);
  D.setTextColor(C_MUTED, C_BG);
  D.setCursor(6, 107);
  D.print(blePickerStatus);
  // The device picker is also the offline landing page.
  // Reconnection must never require the main menu or a reboot.
  drawFooter();
}

void drawBlePinScreen() {
  D.fillScreen(C_BG);
  drawHeader("NodePad / Bluetooth");
  drawSectionTitle("PAIRING PIN");

  D.setTextSize(1);
  D.setTextColor(C_MUTED, C_BG);
  D.setCursor(8, 44);
  D.print("COMPANION");
  D.setTextColor(C_BLUE, C_BG);
  String name = (bleConnectChoice >= 0 && bleConnectChoice < bleChoiceCount)
                 ? bleChoices[bleConnectChoice].name : "Unknown";
  if (name.length() > 24) name = name.substring(0, 24);
  D.setCursor(8, 56);
  D.print(name);

  D.setTextColor(C_WHITE, C_BG);
  D.setTextSize(2);
  D.setCursor(22, 73);
  D.print(blePinInput);
  for (int i = blePinInput.length(); i < 6; ++i) D.print('_');

  D.setTextSize(1);
  D.setTextColor(C_BLUE, C_BG);
  D.setCursor(6, 104);
  D.print(blePinStatus);
  drawFooter();
}

void drawConnectionScreen(const char* line1, const char* line2) {
  D.fillScreen(C_BG);

  centerText("NodePad", 28, 2.2, C_BLUE);
  centerText("OFF-GRID MESH TERMINAL", 58, 1.0, C_BLUE);

  D.drawFastHLine(24, 82, 192, C_BLUE_DIM);

  centerText(line1 ? line1 : "", 91, 1.1, C_WHITE);

  if (line2) {
    centerText(line2, 108, 0.9, C_BLUE);
  }

  // One atomic transfer to the LCD.
  D.pushSprite(0, 0);
}

void drawCurrentScreen() {
  switch (currentScreen) {
    case SCREEN_MAIN:           drawMainScreen();          break;
    case SCREEN_CHAT:           drawChatScreen();          break;
    case SCREEN_PRIVATE_LIST:   drawPrivateListScreen();   break;
    case SCREEN_PRIVATE_CHAT:   drawPrivateChatScreen();   break;
    case SCREEN_NODES:          drawNodesScreen();         break;
    case SCREEN_PING:           drawPingScreen();          break;
    case SCREEN_LIVE_REPEATERS: drawLiveRepeatersScreen(); break;
    case SCREEN_ADVERT:         drawAdvertScreen();        break;
    case SCREEN_SYSTEM:         drawSystemScreen();        break;
    case SCREEN_WEB_SETUP:      drawRadioSetupScreen();    break;
    case SCREEN_CREDITS:        drawCreditsScreen();       break;
    case SCREEN_BLE_DEVICES:    drawBleDevicesScreen();    break;
    case SCREEN_BLE_PIN:        drawBlePinScreen();        break;
  }

  // Full UI is now composed off-screen, then pushed once.
  D.pushSprite(0, 0);

  uiDirty = false;
  lastUiRefresh = millis();
}

// ============================================================
// KEYBOARD / NAVIGATION
// ============================================================

void setScreen(ScreenId s) {
  currentScreen = s;
  uiDirty = true;

  if (s == SCREEN_PRIVATE_LIST || s == SCREEN_NODES ||
      s == SCREEN_PING || s == SCREEN_LIVE_REPEATERS) {
    if (!contactsLoaded) requestContactsRefresh();
  }

  if (s == SCREEN_LIVE_REPEATERS) {
    repeaterScanRequested = true;
  }
}

void goBack() {
  if (currentScreen == SCREEN_MAIN) return;
  if (currentScreen == SCREEN_WEB_SETUP) { stopSetupAccessPoint(); setScreen(SCREEN_SYSTEM); return; }
  if (currentScreen == SCREEN_CREDITS) { setScreen(SCREEN_SYSTEM); return; }
  if (currentScreen == SCREEN_BLE_DEVICES) {
    // No offline main screen: stay on the connect page if disconnected.
    if (bleConnected && meshClient && meshClient->isConnected()) {
      setScreen(SCREEN_MAIN);
    }
    return;
  }
  if (currentScreen == SCREEN_BLE_PIN) {
    setScreen(SCREEN_BLE_DEVICES);
    return;
  }
  if (currentScreen == SCREEN_PRIVATE_CHAT) {
    setScreen(privateReturnScreen);
    return;
  }
  setScreen(SCREEN_MAIN);
}

void moveSelection(int delta) {
  switch (currentScreen) {
    case SCREEN_MAIN:
      mainSelection += delta;
      if (mainSelection < 0) mainSelection = 6;
      if (mainSelection > 6) mainSelection = 0;
      break;

    case SCREEN_PRIVATE_LIST: {
      int total = getChatContactCount();
      if (total > 0) {
        privateListSelection += delta;
        if (privateListSelection < 0) privateListSelection = total - 1;
        if (privateListSelection >= total) privateListSelection = 0;
      }
      break;
    }

    case SCREEN_NODES:
      if (contactCount > 0) {
        nodesSelection += delta;
        if (nodesSelection < 0) nodesSelection = contactCount - 1;
        if (nodesSelection >= contactCount) nodesSelection = 0;
      }
      break;

    case SCREEN_PING: {
      int total = getRepeaterCount();
      if (total > 0) {
        pingSelection += delta;
        if (pingSelection < 0) pingSelection = total - 1;
        if (pingSelection >= total) pingSelection = 0;
      }
      break;
    }

    case SCREEN_LIVE_REPEATERS: {
      int total = getLiveRepeaterCount();
      if (total > 0) {
        liveRepeaterSelection += delta;
        if (liveRepeaterSelection < 0) liveRepeaterSelection = total - 1;
        if (liveRepeaterSelection >= total) liveRepeaterSelection = 0;
      }
      break;
    }

    case SCREEN_BLE_DEVICES:
      if (bleChoiceCount > 0) {
        bleListSelection += delta;
        if (bleListSelection < 0) bleListSelection = bleChoiceCount - 1;
        if (bleListSelection >= bleChoiceCount) bleListSelection = 0;
      }
      break;

    case SCREEN_ADVERT:
      advertSelection += delta;
      if (advertSelection < 0) advertSelection = 1;
      if (advertSelection > 1) advertSelection = 0;
      break;

    default:
      break;
  }

  uiDirty = true;
}

void activateSelection() {
  switch (currentScreen) {
    case SCREEN_MAIN:
      switch (mainSelection) {
        case 0: setScreen(SCREEN_CHAT);           break;
        case 1: setScreen(SCREEN_PRIVATE_LIST);   break;
        case 2: setScreen(SCREEN_NODES);          break;
        case 3: setScreen(SCREEN_PING);           break;
        case 4: setScreen(SCREEN_LIVE_REPEATERS); break;
        case 5: setScreen(SCREEN_ADVERT);         break;
        case 6: setScreen(SCREEN_SYSTEM);         break;
      }
      break;

    case SCREEN_CHAT:
      sendChatMessage();
      break;

    case SCREEN_PRIVATE_LIST: {
      int cidx = getChatContactIndex(privateListSelection);
      if (cidx >= 0) openPrivateChat(cidx);
      break;
    }

    case SCREEN_PRIVATE_CHAT:
      sendPrivateMessage();
      break;

    case SCREEN_NODES:
      if (nodesSelection >= 0 && nodesSelection < contactCount &&
          contacts[nodesSelection].type == ADV_TYPE_CHAT) {
        openPrivateChat(nodesSelection);
      }
      break;

    case SCREEN_PING:
      sendPingToSelectedRepeater();
      break;

    case SCREEN_LIVE_REPEATERS:
      sendPathToSelectedLiveRepeater();
      break;

    case SCREEN_ADVERT:
      sendAdvert(advertSelection == 1);
      break;

    case SCREEN_SYSTEM:
      startBleDeviceSelection();
      break;

    case SCREEN_BLE_DEVICES:
      if (bleScanInProgress) break; // Wait for scan completion.
      bleAutoReconnect = false; // User explicitly selects a radio.
      if (bleChoiceCount > 0) {
        openBlePinScreen(bleListSelection);
      } else {
        bleScanRequested = true;
      }
      break;

    case SCREEN_BLE_PIN:
      submitBlePin();
      break;

    default:
      break;
  }
}

void handleKeyboard() {
  M5Cardputer.update();

  if (!M5Cardputer.Keyboard.isChange() ||
      !M5Cardputer.Keyboard.isPressed()) {
    return;
  }

  Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

  // Use the raw Cardputer key map as well as normal W/S navigation.
  // This is deliberately compatible with older M5Cardputer library releases:
  // Fn+; = up, Fn+. = down, Fn+, = left, Fn+/ = right.
  bool navUp = false;
  bool navDown = false;
  bool navLeft = false;
  bool navRight = false;
  bool escapePressed = false;

  for (char c : status.word) {
    // The physical red ESC key is reported as backtick by common Cardputer builds.
    if (c == '`' || c == '~') escapePressed = true;

    if (status.fn) {
      if (c == ';' || c == ':') navUp = true;
      if (c == '.' || c == '>') navDown = true;
      if (c == ',' || c == '<') navLeft = true;
      if (c == '/' || c == '?') navRight = true;
    }
  }

  if (escapePressed) {
    goBack();
    return;
  }

  // Six-digit PIN is edited on this page, NOT inside the BLE callback.
  // Typing a digit replaces the prefilled PIN on first keystroke.
  if (currentScreen == SCREEN_BLE_PIN) {
    if (status.del) {
      blePinReplaceOnDigit = false;
      if (blePinInput.length()) blePinInput.remove(blePinInput.length() - 1);
      uiDirty = true;
      return;
    }
    if (status.enter) {
      submitBlePin();
      return;
    }
    if (!status.fn) {
      for (char c : status.word) {
        if (c >= '0' && c <= '9') {
          if (blePinReplaceOnDigit) {
            blePinInput = "";
            blePinReplaceOnDigit = false;
          }
          if (blePinInput.length() < 6) blePinInput += c;
          uiDirty = true;
        }
      }
    }
    return;
  }

  // On Chat, printable input should be actual message text.
  if (currentScreen == SCREEN_CHAT) {
    if (status.del) {
      if (chatInput.length() > 0) {
        chatInput.remove(chatInput.length() - 1);
        uiDirty = true;
      }
    }

    if (status.enter) {
      sendChatMessage();
      return;
    }

    if (!status.fn) {
      for (char c : status.word) {
        if (c >= 32 && c <= 126 && chatInput.length() < 120) {
          chatInput += c;
          uiDirty = true;
        }
      }
    }
    return;
  }

  // Private chat has its own input buffer and sends direct contact messages.
  if (currentScreen == SCREEN_PRIVATE_CHAT) {
    if (status.del) {
      if (privateInput.length() > 0) {
        privateInput.remove(privateInput.length() - 1);
        uiDirty = true;
      }
    }

    if (status.enter) {
      sendPrivateMessage();
      return;
    }

    if (!status.fn) {
      for (char c : status.word) {
        if (c >= 32 && c <= 126 && privateInput.length() < 120) {
          privateInput += c;
          uiDirty = true;
        }
      }
    }
    return;
  }

  if (navUp) {
    moveSelection(-1);
    return;
  }

  if (navDown) {
    moveSelection(+1);
    return;
  }

  if (navLeft) {
    goBack();
    return;
  }

  if (navRight || status.enter) {
    activateSelection();
    return;
  }

  // Printable shortcuts.
  for (char c : status.word) {
    if (c == 'w' || c == 'W') {
      moveSelection(-1);
      return;
    }

    if (c == 's' || c == 'S') {
      moveSelection(+1);
      return;
    }

    if (c == 'r' || c == 'R') {
      if (currentScreen == SCREEN_BLE_DEVICES) {
        if (bleScanInProgress) return;
        bleAutoReconnect = false;
        bleScanRequested = true;
        snprintf(blePickerStatus, sizeof(blePickerStatus), "SCANNING...");
        uiDirty = true;
        return;
      }
      if (currentScreen == SCREEN_PRIVATE_LIST ||
          currentScreen == SCREEN_NODES ||
          currentScreen == SCREEN_PING) {
        requestContactsRefresh();
        uiDirty = true;
      } else if (currentScreen == SCREEN_LIVE_REPEATERS) {
        repeaterScanRequested = true;
        snprintf(repeaterScanStatus, sizeof(repeaterScanStatus), "SCAN REQUESTED...");
        uiDirty = true;
      }
      return;
    }

    if (currentScreen == SCREEN_SYSTEM && (c == 'p' || c == 'P')) {
      if (startSetupAccessPoint()) setScreen(SCREEN_WEB_SETUP);
      return;
    }
    if (currentScreen == SCREEN_SYSTEM && (c == 'c' || c == 'C')) {
      setScreen(SCREEN_CREDITS);
      return;
    }
    // M cycles LOW -> MEDIUM -> MUTE; preserve compatibility with older sound_on.
    if (currentScreen == SCREEN_SYSTEM && (c == 'm' || c == 'M')) {
      notificationSoundLevel = (notificationSoundLevel + 1) % 3;
      nodePadPreferences.putUChar("sound_lvl", notificationSoundLevel);
      nodePadPreferences.putBool("sound_on", notificationSoundLevel != 0);
      M5Cardputer.Speaker.setVolume(selectedNotificationVolume());
      uiDirty = true;
      return;
    }

    // Direct main-menu shortcuts.
    if (currentScreen == SCREEN_MAIN) {
      if (c >= '1' && c <= '7') {
        mainSelection = c - '1';
        activateSelection();
        return;
      }
    }
  }
}

// ============================================================
// SETUP / LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(100);

  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Speaker.setVolume(SOFT_NOTIFICATION_VOLUME);

  LCD.setRotation(1);
  LCD.setBrightness(100);

  // 240x135 off-screen framebuffer. All existing graphics are drawn into
  // this canvas unchanged and transferred to the LCD in a single push.
  D.setColorDepth(16);
  if (D.createSprite(LCD.width(), LCD.height()) == nullptr) {
    LCD.fillScreen(TFT_BLACK);
    LCD.setTextColor(TFT_RED);
    LCD.setCursor(8, 20);
    LCD.print("FRAMEBUFFER ERROR");
    while (true) delay(100);
  }

  D.setTextFont(1);
  D.setTextWrap(false);

  setBlueTheme();
  updateBattery(true);

  // NodePad release splash before BLE work starts.
  showBootScreen();

  drawConnectionScreen("INITIALIZING BLE", nullptr);

  configureRadioWeb();
  NimBLEDevice::init("");
  NimBLEDevice::setMTU(512);

  // Secure client settings for the MeshCore PIN-protected BLE link.
  NimBLEDevice::setSecurityAuth(true, true, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);

  // Remember the Companion across reboots, but never automatically select a
  // different radio when the known one is offline.
  nodePadPreferences.begin("nodepad", false);
  preferredBleAddress = nodePadPreferences.getString("last_radio", "");
  // On upgrade, map the old saved bool to LOW or MUTE.
  notificationSoundLevel = nodePadPreferences.getUChar("sound_lvl",
      nodePadPreferences.getBool("sound_on", true) ? 1 : 0);
  if (notificationSoundLevel > 2) notificationSoundLevel = 1;
  M5Cardputer.Speaker.setVolume(selectedNotificationVolume());
  Serial.printf("[BLE] Stored Companion: %s\n", preferredBleAddress.c_str());

  // Boot -> async scan -> remembered radio (or sole radio) -> main menu.
  // Otherwise stay on the device connection page.
  startBleDeviceSelection();
  bleConnectAutomatically = true;
  drawCurrentScreen();
}

void loop() {
  if(webActive)radioWeb.handleClient();
  if((saveStage==SAVE_WAIT_ACK || saveStage==SAVE_QUEUED) && (!bleConnected || !meshOnline)){
    saveStage=SAVE_ERR;
    snprintf(saveDetail,sizeof(saveDetail),"Companion disconnected - verify settings");
  }
  handleKeyboard();

  // BLE manual picker: never reconnect to the old device behind this screen.
  if (bleManualSelection) {
    if (bleScanRequested && !bleScanInProgress &&
        (!meshClient || !meshClient->isConnected()) &&
        millis() - manualDisconnectAt >= 250) {
      scanMeshCoreDevices();
    } else if (bleScanInProgress) {
      NimBLEScan* scan = NimBLEDevice::getScan();
      // A local watchdog guarantees UI recovery even if BLE never sends
      // its scan-ended event. We do not call blocking getResults(duration).
      if (!scan->isScanning() || millis() - bleScanStartedAt > 10000UL) {
        finishBleScan();
      }
    } else if (bleConnectRequested && !bleScanRequested) {
      bleConnectRequested = false;
      const int index = bleConnectChoice;
      if (index >= 0 && index < bleChoiceCount) {
        const String address = bleChoices[index].address;
        if (connectMeshCore(address, true)) {
          setScreen(SCREEN_MAIN);
        } else {
          // connectMeshCore already left a specific error message on the picker.
          // Do not automatically retry or toggle between screens.
          bleManualSelection = true;
          setScreen(SCREEN_BLE_DEVICES);
        }
      }
    }
  } else if (!bleConnected || !meshClient || !meshClient->isConnected()) {
    if (millis() - lastReconnectAttempt >= 5000) {
      // After unexpected link loss, scan and reconnect ONLY to the
      // previously selected Companion. Show the picker if unavailable.
      lastReconnectAttempt = millis();
      startBleDeviceSelection();
      bleAutoReconnect = preferredBleAddress.length() > 0;
      uiDirty = true;
    }
  } else {
    serviceProtocol();
  }

  if (notificationBeepRequested) {
    notificationBeepRequested = false;
    if (millis() - lastBeepAt >= 600) {
      if (notificationSoundLevel != 0) M5Cardputer.Speaker.tone(1250, 45);
      lastBeepAt = millis();
    }
  }

  // Battery is deliberately sampled slowly so its ADC estimate does not
  // bounce around and trigger unnecessary UI work.
  updateBattery(false);

  // Smooth advert TX animation without flicker (canvas is pushed atomically).
  if (currentScreen == SCREEN_ADVERT &&
      (advertPending || millis() < advertSuccessUntil) &&
      millis() - lastAdvertAnimTick >= 120) {
    lastAdvertAnimTick = millis();
    advertAnimPhase = (advertAnimPhase + 1) % 3;
    uiDirty = true;
  }

  // Animate the live-scan status lightly while listening for repeaters.
  if (currentScreen == SCREEN_LIVE_REPEATERS && repeaterScanning &&
      millis() - lastAdvertAnimTick >= 180) {
    lastAdvertAnimTick = millis();
    uiDirty = true;
  }

  // Refresh dynamic header/status once per second (battery/link state).
  if (millis() - lastUiRefresh >= 1000) {
    uiDirty = true;
  }

  if (uiDirty) {
    drawCurrentScreen();
  }

  delay(5);
}
