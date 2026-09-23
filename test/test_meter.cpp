#include <unity.h>

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
  f = meter_drive_fill(60, 140);
  TEST_ASSERT_EQUAL(70, f.pos_w);
  TEST_ASSERT_EQUAL(0, f.neg_w);
  f = meter_drive_fill(-30, 140);
  TEST_ASSERT_EQUAL(70, f.neg_w);
  TEST_ASSERT_EQUAL(0, f.pos_w);
  f = meter_drive_fill(999, 140);
  TEST_ASSERT_EQUAL(140, f.pos_w);
  f = meter_drive_fill(-999, 140);
  TEST_ASSERT_EQUAL(140, f.neg_w);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_park_selects_full_bar_charge);
  RUN_TEST(test_reverse_neutral_drive_stay_split);
  RUN_TEST(test_charge_fill_clamps);
  RUN_TEST(test_drive_fill_splits);
  return UNITY_END();
}
