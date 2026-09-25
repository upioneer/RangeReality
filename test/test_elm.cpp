#include <string.h>
#include <unity.h>

#include "../src/obd/elm.cpp"
#include "../src/state.cpp"

// Fake clock: advanced by the script transport on empty reads so the
// protocol timeouts terminate without real waiting.
static uint32_t s_now = 0;
uint32_t millis() {
  return s_now;
}

struct ScriptTransport : ElmTransport {
  const char **lines;
  int count;
  int idx = 0;
  bool send(const char *cmd) override {
    (void)cmd;
    return true;
  }
  bool recvLine(char *buf, size_t n, uint32_t timeout_ms) override {
    if (idx >= count) {
      s_now += timeout_ms;
      return false;
    }
    strncpy(buf, lines[idx++], n - 1);
    buf[n - 1] = '\0';
    return true;
  }
};

void setUp(void) {
  s_now = 0;
}
void tearDown(void) {}

// --- Truck case: sim-only PIDs answer NO DATA -------------------------------

void test_query_no_data_returns_false(void) {
  const char *script[] = {"NO DATA", ">"};
  ScriptTransport t{script, 2};
  char resp[96] = {0};
  TEST_ASSERT_FALSE(elm::query(t, "226101", resp, sizeof(resp)));
}

void test_gear_no_data_returns_zero_not_stale(void) {
  const char *script[] = {"NO DATA", ">"};
  ScriptTransport t{script, 2};
  TEST_ASSERT_EQUAL(0, elm::gear(t));
}

void test_pack_no_data_returns_false(void) {
  const char *script[] = {"NO DATA", ">"};
  ScriptTransport t{script, 2};
  float v = -1.0f, a = -1.0f;
  TEST_ASSERT_FALSE(elm::packVI(t, v, a));
}

// --- Sim path guards: valid replies still parse ------------------------------

void test_query_skips_echo_returns_data(void) {
  const char *script[] = {"010D", "41 0D 5A", ">"};
  ScriptTransport t{script, 3};
  char resp[96] = {0};
  TEST_ASSERT_TRUE(elm::query(t, "010D", resp, sizeof(resp)));
  TEST_ASSERT_EQUAL_STRING("41 0D 5A", resp);
  float spd = 0.0f;
  TEST_ASSERT_TRUE(elm::decode01(resp, 0x0D, spd));
  TEST_ASSERT_EQUAL_FLOAT(90.0f, spd);
}

void test_gear_sim_drive(void) {
  const char *script[] = {"62 61 03 44", ">"};
  ScriptTransport t{script, 2};
  TEST_ASSERT_EQUAL('D', elm::gear(t));
}

void test_pack_sim_values(void) {
  const char *both[] = {"62 61 01 05 5F 00", ">", "62 61 02 FF FC 18", ">"};
  ScriptTransport t{both, 4};
  float v = 0.0f, a = 0.0f;
  TEST_ASSERT_TRUE(elm::packVI(t, v, a));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 392.192f, v);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, a);
}

// --- Offline stub must not invent a shifter position after live data ---------

void test_stub_freezes_gear_once_live_seen(void) {
  state_stub_update();  // offline demo drives the shifter
  TEST_ASSERT_EQUAL((int)Gear::DRIVE, (int)g_state.gear);
  state_set_live_seen();
  g_state.gear = Gear::REVERSE;
  g_state.speed_kmh = 40.0f;
  state_stub_update();
  TEST_ASSERT_EQUAL(0, g_state.kw);
  TEST_ASSERT_EQUAL((int)Gear::REVERSE, (int)g_state.gear);
  TEST_ASSERT_EQUAL_FLOAT(40.0f, g_state.speed_kmh);
}
