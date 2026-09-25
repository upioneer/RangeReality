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

void test_failure_records_last_line_and_elapsed(void) {
  const char *script[] = {"NO DATA", ">"};
  ScriptTransport t{script, 2};
  char resp[96] = {0};
  TEST_ASSERT_FALSE(elm::query(t, "226101", resp, sizeof(resp)));
  TEST_ASSERT_EQUAL_STRING("NO DATA", elm::last_line);
}

void test_timeout_records_elapsed(void) {
  ScriptTransport t{nullptr, 0};
  char resp[96] = {0};
  TEST_ASSERT_FALSE(elm::query(t, "010D", resp, sizeof(resp)));
  TEST_ASSERT_TRUE(elm::last_ms >= 600);
  TEST_ASSERT_EQUAL_STRING("", elm::last_line);
}

void test_success_records_last_line(void) {
  const char *script[] = {"010D", "41 0D 5A", ">"};
  ScriptTransport t{script, 3};
  char resp[96] = {0};
  TEST_ASSERT_TRUE(elm::query(t, "010D", resp, sizeof(resp)));
  TEST_ASSERT_EQUAL_STRING("41 0D 5A", elm::last_line);
}

void test_query_strips_11bit_can_header(void) {
  // Truck with ATH1: header HHH + length LL ahead of the payload.
  const char *script[] = {"7EF03410D00", ">"};
  ScriptTransport t{script, 2};
  char resp[96] = {0};
  TEST_ASSERT_TRUE(elm::query(t, "010D", resp, sizeof(resp)));
  TEST_ASSERT_EQUAL_STRING("410D00", resp);
  float spd = -1.0f;
  TEST_ASSERT_TRUE(elm::decode01(resp, 0x0D, spd));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, spd);
}

void test_query_strips_header_bitmask(void) {
  const char *script[] = {"7EF06410098180003", ">"};
  ScriptTransport t{script, 2};
  char resp[96] = {0};
  TEST_ASSERT_TRUE(elm::query(t, "0100", resp, sizeof(resp)));
  TEST_ASSERT_EQUAL_STRING("410098180003", resp);
}

void test_query_ignores_multiframe(void) {
  const char *script[] = {"7E81014490200", ">"};
  ScriptTransport t{script, 2};
  char resp[96] = {0};
  TEST_ASSERT_FALSE(elm::query(t, "0902", resp, sizeof(resp)));
}

void test_decode01_soc(void) {
  // 7EC03415BB8 from the truck: B8 = 72.2 %.
  float soc = -1.0f;
  TEST_ASSERT_TRUE(elm::decode01("415BB8", 0x5B, soc));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 72.16f, soc);
}

void test_set_header_ok(void) {
  const char *script[] = {"OK", ">"};
  ScriptTransport t{script, 2};
  TEST_ASSERT_TRUE(elm::set_header(t, "7E4"));
}

void test_set_header_rejected(void) {
  const char *script[] = {"?", ">"};
  ScriptTransport t{script, 2};
  TEST_ASSERT_FALSE(elm::set_header(t, "7E4"));
}

void test_decode22_sim_pid(void) {
  uint8_t out[8] = {0};
  TEST_ASSERT_EQUAL(3, elm::decode22("62 61 01 05 5F 00", 0x6101, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0x05, out[0]);
  TEST_ASSERT_EQUAL(0x5F, out[1]);
  TEST_ASSERT_EQUAL(0x00, out[2]);
}

void test_decode22_headed_truck_pid(void) {
  // query() strips 7E806 first; decode22 sees 62480A00C8.
  uint8_t out[8] = {0};
  TEST_ASSERT_EQUAL(2, elm::decode22("62480A00C8", 0x480A, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0x00, out[0]);
  TEST_ASSERT_EQUAL(0xC8, out[1]);
}

void test_decode22_mismatch(void) {
  uint8_t out[8] = {0};
  TEST_ASSERT_EQUAL(-1, elm::decode22("62480A00C8", 0x480B, out, sizeof(out)));
  TEST_ASSERT_EQUAL(-1, elm::decode22("410D00", 0x480A, out, sizeof(out)));
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

void test_trip_clock_integrates_live_speed(void) {
  g_ble = BleLink::CONNECTED;
  g_state.speed_kmh = 90.0f;
  g_state.trip_sec = 0;
  g_state.trip_mi = 0.0f;
  s_now = 1000;
  state_clock_update();  // arms the clock, adds nothing
  TEST_ASSERT_EQUAL(0, g_state.trip_sec);
  s_now = 2000;
  state_clock_update();  // +1 s at 90 km/h = 0.0155 mi
  TEST_ASSERT_EQUAL(1, g_state.trip_sec);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.01553f, g_state.trip_mi);
}

void test_trip_clock_freezes_miles_offline(void) {
  g_ble = BleLink::SCANNING;
  g_state.speed_kmh = 101.0f;  // stub cruise must not invent miles
  float mi = g_state.trip_mi;
  uint32_t sec = g_state.trip_sec;
  s_now += 2000;
  state_clock_update();
  TEST_ASSERT_EQUAL(sec + 2, g_state.trip_sec);
  TEST_ASSERT_EQUAL_FLOAT(mi, g_state.trip_mi);
}

void test_vehicle_charging_triggers(void) {
  g_state.gear = Gear::UNKNOWN;
  g_state.chg_w = -1;
  TEST_ASSERT_FALSE(vehicle_charging());
  g_state.chg_w = 11500;  // validated charge session, no shifter needed
  TEST_ASSERT_TRUE(vehicle_charging());
  g_state.chg_w = -1;
  g_state.gear = Gear::PARK;
  TEST_ASSERT_TRUE(vehicle_charging());
  g_state.gear = Gear::UNKNOWN;
  g_state.chg_w = 0;
  TEST_ASSERT_FALSE(vehicle_charging());
}

void test_charge_avg_empty_is_unknown(void) {
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, charge_kw_avg());
  TEST_ASSERT_EQUAL(-1, charge_target_pct());
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, charge_eta_hours());
}

void test_charge_eta_at_truck_numbers(void) {
  // socd 75.5 %, charger at 11500 W: (4.5 x 131 / 100) / 11.5 = 0.513 h.
  g_state.soc_disp = 75.5f;
  g_state.soc_pct = 72.0f;
  TEST_ASSERT_EQUAL(80, charge_target_pct());
  charge_note_power(11500);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.5f, charge_kw_avg());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5126f, charge_eta_hours());
  for (int i = 0; i < 7; i++) charge_note_power(11500);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.5f, charge_kw_avg());
}

void test_session_kwh_integrates_charge_power(void) {
  // 11500 W for 7 s = 22.36 Wh. A gap (0 W) ends the session; the next
  // power restarts the total instead of adding to it.
  s_now = 11000;
  charge_note_power(11500);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.02236f, g_state.chg_kwh);
  charge_note_power(0);
  s_now = 13000;
  charge_note_power(11500);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, g_state.chg_kwh);
}

void test_charge_target_switches_at_eighty(void) {
  g_state.soc_disp = 85.0f;
  TEST_ASSERT_EQUAL(100, charge_target_pct());
  g_state.soc_disp = 100.0f;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, charge_eta_hours());
  g_state.soc_disp = -1.0f;
  g_state.soc_pct = -1.0f;
}

void test_trip_clock_pauses_in_park(void) {
  g_ble = BleLink::CONNECTED;
  g_state.gear = Gear::PARK;
  g_state.speed_kmh = 0.0f;
  uint32_t sec = g_state.trip_sec;
  float mi = g_state.trip_mi;
  s_now += 5000;
  state_clock_update();
  TEST_ASSERT_EQUAL(sec, g_state.trip_sec);
  TEST_ASSERT_EQUAL_FLOAT(mi, g_state.trip_mi);
  g_state.gear = Gear::UNKNOWN;
}

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

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_query_no_data_returns_false);
  RUN_TEST(test_gear_no_data_returns_zero_not_stale);
  RUN_TEST(test_pack_no_data_returns_false);
  RUN_TEST(test_query_skips_echo_returns_data);
  RUN_TEST(test_query_strips_11bit_can_header);
  RUN_TEST(test_query_strips_header_bitmask);
  RUN_TEST(test_query_ignores_multiframe);
  RUN_TEST(test_decode01_soc);
  RUN_TEST(test_set_header_ok);
  RUN_TEST(test_set_header_rejected);
  RUN_TEST(test_decode22_sim_pid);
  RUN_TEST(test_decode22_headed_truck_pid);
  RUN_TEST(test_decode22_mismatch);
  RUN_TEST(test_failure_records_last_line_and_elapsed);
  RUN_TEST(test_timeout_records_elapsed);
  RUN_TEST(test_success_records_last_line);
  RUN_TEST(test_gear_sim_drive);
  RUN_TEST(test_pack_sim_values);
  RUN_TEST(test_trip_clock_integrates_live_speed);
  RUN_TEST(test_trip_clock_freezes_miles_offline);
  RUN_TEST(test_trip_clock_pauses_in_park);
  RUN_TEST(test_charge_avg_empty_is_unknown);
  RUN_TEST(test_charge_eta_at_truck_numbers);
  RUN_TEST(test_session_kwh_integrates_charge_power);
  RUN_TEST(test_charge_target_switches_at_eighty);
  RUN_TEST(test_vehicle_charging_triggers);
  RUN_TEST(test_stub_freezes_gear_once_live_seen);
  return UNITY_END();
}
