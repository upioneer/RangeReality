#include "obd/ble_link.h"

#include <NimBLEDevice.h>

#include "state.h"

// Adapter profiles: name fragment + GATT UART doorway.
// VEEPEAK entry shares the clone doorway until the truck reveals its
// real UUIDs via scan log, then it gets its own row.
struct ObdProfile {
  const char *tag;
  const char *name_frag;
  const char *svc;
  const char *chr;
};

static const ObdProfile kProfiles[] = {
    {"OBDSIM", "OBDSIM", "FFE0", "FFE1"},
    {"VEEPEAK", "OBD", "FFE0", "FFE1"},
};

static NimBLEClient *s_client = nullptr;
static NimBLERemoteCharacteristic *s_chr = nullptr;
static bool s_have_target = false;
static NimBLEAddress s_target = NimBLEAddress("");
static const ObdProfile *s_profile = nullptr;
static bool s_inited = false;
static uint32_t s_last_attempt = 0;
static uint32_t s_last_poll = 0;
static uint8_t s_poll_step = 0;

// OBD poll period. 500 ms proved too aggressive, back to 1000.
// TODO(polish): retest faster rates against real adapter latency.
#define OBD_POLL_MS 1000

// Notify assembly buffer (BLE task context).
static char s_rx[256];
static size_t s_rx_len = 0;

static void onNotify(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool isNotify) {
  (void)c;
  (void)isNotify;
  for (size_t i = 0; i < len && s_rx_len < sizeof(s_rx) - 1; i++) {
    s_rx[s_rx_len++] = (char)data[i];
  }
}

class ScanCb : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *adv) override {
    std::string name = adv->getName();
    for (const auto &p : kProfiles) {
      bool name_hit = name.find(p.name_frag) != std::string::npos;
      bool svc_hit = adv->isAdvertisingService(NimBLEUUID(p.svc));
      if (name_hit || svc_hit) {
        Serial.printf("[ble] match %s name=%s rssi=%d\n", p.tag, name.c_str(), adv->getRSSI());
        s_target = adv->getAddress();
        s_profile = &p;
        s_have_target = true;
        NimBLEDevice::getScan()->stop();
        return;
      }
    }
    if (!name.empty()) {
      Serial.printf("[ble] seen %s rssi=%d\n", name.c_str(), adv->getRSSI());
    }
  }
};

class ClientCb : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *c) override {
    (void)c;
    Serial.println("[ble] client connected");
  }
  void onDisconnect(NimBLEClient *c) override {
    (void)c;
    Serial.println("[ble] disconnected");
    s_chr = nullptr;
    s_inited = false;
    g_ble = BleLink::SCANNING;
  }
};

struct LinkTransport : public ElmTransport {
  bool send(const char *cmd) override {
    if (s_chr == nullptr) return false;
    String full(cmd);
    full += '\r';
    Serial.printf("[obd] > %s\n", cmd);
    return s_chr->writeValue(full.c_str(), false);
  }
  bool recvLine(char *buf, size_t n, uint32_t timeout_ms) override {
    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
      if (s_chr == nullptr) return false;
      char *cr = (char *)memchr(s_rx, '\r', s_rx_len);
      if (cr != nullptr) {
        size_t len = (size_t)(cr - s_rx);
        if (len >= n) len = n - 1;
        memcpy(buf, s_rx, len);
        buf[len] = '\0';
        size_t used = (size_t)(cr - s_rx) + 1;
        memmove(s_rx, s_rx + used, s_rx_len - used);
        s_rx_len -= used;
        if (strlen(buf) == 0) continue;
        return true;
      }
      delay(2);
    }
    return false;
  }
};

static LinkTransport s_transport;

static bool do_connect(void) {
  if (s_client == nullptr) {
    s_client = NimBLEDevice::createClient();
    s_client->setClientCallbacks(new ClientCb(), false);
  }
  Serial.printf("[ble] connecting %s profile=%s\n", s_target.toString().c_str(), s_profile->tag);
  if (!s_client->connect(s_target)) {
    Serial.println("[ble] connect failed");
    return false;
  }
  NimBLERemoteService *svc = s_client->getService(NimBLEUUID(s_profile->svc));
  if (svc == nullptr) {
    Serial.println("[ble] service missing");
    s_client->disconnect();
    return false;
  }
  s_chr = svc->getCharacteristic(NimBLEUUID(s_profile->chr));
  if (s_chr == nullptr || !s_chr->canNotify()) {
    Serial.println("[ble] char missing or no notify");
    s_chr = nullptr;
    s_client->disconnect();
    return false;
  }
  if (!s_chr->subscribe(true, onNotify)) {
    Serial.println("[ble] subscribe failed");
    s_chr = nullptr;
    s_client->disconnect();
    return false;
  }
  s_rx_len = 0;
  Serial.println("[ble] subscribed, running ELM init");
  if (!elm::init(s_transport)) {
    Serial.println("[obd] ELM init failed");
    s_chr = nullptr;
    s_client->disconnect();
    return false;
  }
  Serial.println("[obd] ELM init OK");
  s_inited = true;
  g_ble = BleLink::CONNECTED;
  return true;
}

static void do_poll(void) {
  if (s_poll_step % 4 == 3) {
    char g = elm::gear(s_transport);
    Gear next = Gear::UNKNOWN;
    if (g == 'P') next = Gear::PARK;
    else if (g == 'R') next = Gear::REVERSE;
    else if (g == 'N') next = Gear::NEUTRAL;
    else if (g == 'D') next = Gear::DRIVE;
    if (next != Gear::UNKNOWN && next != g_state.gear) {
      g_state.gear = next;
      Serial.printf("[obd] gear %c\n", g);
    }
  } else if (s_poll_step % 2 == 0) {
    float v, a;
    if (elm::packVI(s_transport, v, a)) {
      int kw = (int)roundf(v * a / 1000.0f);
      g_state.kw = kw;
      Serial.printf("[obd] pack %.1f V %.1f A -> %d kW\n", v, a, kw);
    } else {
      Serial.println("[obd] pack query failed");
    }
  } else {
    char resp[96];
    float spd;
    if (elm::query(s_transport, "010D", resp, sizeof(resp)) && elm::decode01(resp, 0x0D, spd)) {
      Serial.printf("[obd] speed %.0f km/h\n", (double)spd);
    }
  }
  s_poll_step++;
}

void ble_link_init(void) {
  NimBLEDevice::init("LMC");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCb());
  scan->setActiveScan(true);
  g_ble = BleLink::SCANNING;
  Serial.println("[ble] central ready");
}

void ble_link_poll(void) {
  if (s_chr != nullptr && s_inited) {
    if (s_client != nullptr && !s_client->isConnected()) return;
    if (millis() - s_last_poll > OBD_POLL_MS) {
      s_last_poll = millis();
      do_poll();
    }
    return;
  }
  if (s_have_target) {
    s_have_target = false;
    if (do_connect()) return;
    s_last_attempt = millis();
    return;
  }
  if (millis() - s_last_attempt > 4000) {
    s_last_attempt = millis();
    g_ble = BleLink::SCANNING;
    Serial.println("[ble] scanning...");
    NimBLEDevice::getScan()->start(1500, false);
    NimBLEDevice::getScan()->clearResults();
  }
}

const char *ble_state_str(void) {
  switch (g_ble) {
    case BleLink::CONNECTED:
      return "connected";
    case BleLink::SCANNING:
      return "scanning";
    default:
      return "off";
  }
}
