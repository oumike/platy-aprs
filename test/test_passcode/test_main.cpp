#include <unity.h>
#include "aprs/passcode.h"
#include "../../src/aprs/passcode.cpp"

void setUp() {}
void tearDown() {}

static void test_known_values() {
    // Reference values from the canonical aprspass algorithm.
    TEST_ASSERT_EQUAL_INT(13023, aprsPasscode("N0CALL"));
    TEST_ASSERT_EQUAL_INT(aprsPasscode("N0CALL"), aprsPasscode("n0call"));
}

static void test_ssid_ignored() {
    TEST_ASSERT_EQUAL_INT(aprsPasscode("N0CALL"), aprsPasscode("N0CALL-9"));
}

static void test_empty() {
    TEST_ASSERT_EQUAL_INT(-1, aprsPasscode(""));
    TEST_ASSERT_EQUAL_INT(-1, aprsPasscode(nullptr));
    TEST_ASSERT_EQUAL_INT(-1, aprsPasscode("-1"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_known_values);
    RUN_TEST(test_ssid_ignored);
    RUN_TEST(test_empty);
    return UNITY_END();
}
