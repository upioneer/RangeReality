#include <string.h>
#include <unity.h>

#include "../src/advisor.h"
#include "../src/meter.h"

void setUp(void) {}
void tearDown(void) {}

void test_park_selects_full_bar_charge(void) {
  TEST_ASSERT_EQUAL((int)MeterMode::CHARGE, (int)meter_mode(true));
}

void test_reverse_neutral_drive_stay_split(void) {
  // meter_mode takes parked flag: anything not park is drive mode.
  TEST_ASSERT_EQUAL((int)MeterMode::DRIVE, (int)meter_mode(false));
}

void test_charge_fill_clamps(void) {
  TEST_ASSERT_EQUAL(0, meter_charge_fill(0, 276));
  TEST_ASSERT_EQUAL(0, meter_charge_fill(-5, 276));
  TEST_ASSERT_EQUAL(138, meter_charge_fill(100, 276));
  TEST_ASSERT_EQUAL(276, meter_charge_fill(200, 276));
  TEST_ASSERT_EQUAL(276, meter_charge_fill(999, 276));
}

void test_drive_fill_splits(void) {
  MeterDriveFill f = meter_drive_fill(0, 140);
  TEST_ASSERT_EQUAL(0, f.neg_w);
  TEST_ASSERT_EQUAL(0, f.pos_w);
  f = meter_drive_fill(220, 140);
  TEST_ASSERT_EQUAL(70, f.pos_w);
  TEST_ASSERT_EQUAL(0, f.neg_w);
  f = meter_drive_fill(-110, 140);
  TEST_ASSERT_EQUAL(70, f.neg_w);
  TEST_ASSERT_EQUAL(0, f.pos_w);
  f = meter_drive_fill(999, 140);
  TEST_ASSERT_EQUAL(140, f.pos_w);
  f = meter_drive_fill(-999, 140);
  TEST_ASSERT_EQUAL(140, f.neg_w);
}

void test_advisor_charging_state(void) {
  char b[64];
  advisor_pick(120.0f, 0.0f, true, 0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("Charging +120 kW", b);
  advisor_pick(0.0f, 0.0f, true, 0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("Ready to charge", b);
}

void test_advisor_watt_and_regen(void) {
  char b[64];
  advisor_pick(90.0f, 0.0f, false, 0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("Watt in Tarnation!", b);
  advisor_pick(-12.0f, 0.0f, false, 0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("Regen banking energy", b);
}

void test_advisor_aero_tip_is_live(void) {
  char b[64];
  advisor_pick(43.0f, 100.0f, false, 0, b, sizeof(b));
  TEST_ASSERT_TRUE(strstr(b, "63 mph") != nullptr);
  advisor_pick(10.0f, 100.0f, false, 0, b, sizeof(b));
  TEST_ASSERT_TRUE(strstr(b, "63 mph") == nullptr);
}

void test_charge_scale_ac_dc(void) {
  TEST_ASSERT_EQUAL(20, charge_scale_kw(11500, 240.0f));
  TEST_ASSERT_EQUAL(200, charge_scale_kw(150000, 0.0f));
  TEST_ASSERT_EQUAL(200, charge_scale_kw(11500, -1.0f));
  TEST_ASSERT_EQUAL(200, charge_scale_kw(-1, -1.0f));
}

void test_charge_fill_scaled(void) {
  TEST_ASSERT_EQUAL(138, meter_charge_fill(10, 276, 20));
  TEST_ASSERT_EQUAL(276, meter_charge_fill(999, 276, 20));
  TEST_ASSERT_EQUAL(138, meter_charge_fill(100, 276));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_park_selects_full_bar_charge);
  RUN_TEST(test_reverse_neutral_drive_stay_split);
  RUN_TEST(test_charge_fill_clamps);
  RUN_TEST(test_charge_scale_ac_dc);
  RUN_TEST(test_charge_fill_scaled);
  RUN_TEST(test_drive_fill_splits);
  RUN_TEST(test_advisor_charging_state);
  RUN_TEST(test_advisor_watt_and_regen);
  RUN_TEST(test_advisor_aero_tip_is_live);
  return UNITY_END();
}
