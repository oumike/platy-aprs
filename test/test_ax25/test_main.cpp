#include <unity.h>
#include "ax25/ax25.h"

void setUp() {}
void tearDown() {}

static void test_tnc2_roundtrip_through_binary() {
    ax25::Frame f;
    TEST_ASSERT_TRUE(ax25::fromTnc2("N0CALL-9>APRS,WIDE1-1*,WIDE2-1:!4903.50N/07201.75W-Test", f));
    TEST_ASSERT_EQUAL_STRING("N0CALL", f.src.call.c_str());
    TEST_ASSERT_EQUAL_UINT8(9, f.src.ssid);
    TEST_ASSERT_EQUAL_UINT(2, f.digis.size());
    TEST_ASSERT_TRUE(f.digis[0].repeated);
    TEST_ASSERT_FALSE(f.digis[1].repeated);

    auto bin = ax25::encode(f);
    ax25::Frame g;
    TEST_ASSERT_TRUE(ax25::decode(bin.data(), bin.size(), g));
    TEST_ASSERT_EQUAL_STRING("N0CALL-9>APRS,WIDE1-1*,WIDE2-1:!4903.50N/07201.75W-Test",
                             ax25::toTnc2(g).c_str());
}

static void test_star_marks_all_earlier_digis() {
    ax25::Frame f;
    TEST_ASSERT_TRUE(ax25::fromTnc2("A1A>APRS,DIGI1,DIGI2*,WIDE2-1:>hi", f));
    TEST_ASSERT_TRUE(f.digis[0].repeated);
    TEST_ASSERT_TRUE(f.digis[1].repeated);
    TEST_ASSERT_FALSE(f.digis[2].repeated);
    TEST_ASSERT_EQUAL_STRING("A1A>APRS,DIGI1,DIGI2*,WIDE2-1:>hi", ax25::toTnc2(f).c_str());
}

static void test_known_binary_frame() {
    // N0CALL>APRS:>x, no digis.
    const uint8_t raw[] = {'A' << 1, 'P' << 1, 'R' << 1, 'S' << 1, ' ' << 1, ' ' << 1, 0xE0,
                           'N' << 1, '0' << 1, 'C' << 1, 'A' << 1, 'L' << 1, 'L' << 1, 0x61,
                           0x03, 0xF0, '>', 'x'};
    ax25::Frame f;
    TEST_ASSERT_TRUE(ax25::decode(raw, sizeof(raw), f));
    TEST_ASSERT_EQUAL_STRING("N0CALL>APRS:>x", ax25::toTnc2(f).c_str());
    auto enc = ax25::encode(f);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(raw, enc.data(), sizeof(raw));
}

static void test_rejects_bad_input() {
    ax25::Frame f;
    TEST_ASSERT_FALSE(ax25::fromTnc2("no header here", f));
    TEST_ASSERT_FALSE(ax25::fromTnc2("TOOLONGCALL>APRS:x", f));
    TEST_ASSERT_FALSE(ax25::fromTnc2("N0CALL-16>APRS:x", f));
    const uint8_t shortFrame[] = {1, 2, 3};
    TEST_ASSERT_FALSE(ax25::decode(shortFrame, sizeof(shortFrame), f));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_tnc2_roundtrip_through_binary);
    RUN_TEST(test_star_marks_all_earlier_digis);
    RUN_TEST(test_known_binary_frame);
    RUN_TEST(test_rejects_bad_input);
    return UNITY_END();
}
