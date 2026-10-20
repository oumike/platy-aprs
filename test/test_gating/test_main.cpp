#include <unity.h>
#include <string.h>
#include "aprs/lora_frame.h"
#include "aprs/gating.h"

void setUp() {}
void tearDown() {}

static void test_rf_to_is() {
    std::string out;
    TEST_ASSERT_TRUE(gating::rfToIs("N0ABC-9>APRS,WIDE1*,WIDE2-1:!4228.13N/08321.87W>hi\r", "N0CALL-9", out));
    TEST_ASSERT_EQUAL_STRING("N0ABC-9>APRS,WIDE1*,WIDE2-1,qAR,N0CALL-9:!4228.13N/08321.87W>hi", out.c_str());
    TEST_ASSERT_TRUE(gating::rfToIs("N0ABC>APRS:>status", "ME", out));
    TEST_ASSERT_EQUAL_STRING("N0ABC>APRS,qAR,ME:>status", out.c_str());
    TEST_ASSERT_FALSE(gating::rfToIs("N0ABC>APRS,NOGATE:>x", "ME", out));
    TEST_ASSERT_FALSE(gating::rfToIs("N0ABC>APRS,RFONLY*:>x", "ME", out));
    TEST_ASSERT_FALSE(gating::rfToIs("N0ABC>APRS:?APRS?", "ME", out));
    TEST_ASSERT_FALSE(gating::rfToIs("N0ABC>APRS:", "ME", out));
    // Third-party from an IS->RF gate is never sent back.
    TEST_ASSERT_FALSE(gating::rfToIs("N2IGT-1>APDW18,N1DIG,WIDE1*:}APRSPH>APRS,TCPIP,N2IGT-1*::N0CALL-9 :hi", "ME", out));
    // Other third-party traffic is unwrapped.
    TEST_ASSERT_TRUE(gating::rfToIs("GW>APRS,WIDE1*:}N0ABC>APRS,WIDE2-1:>inner", "ME", out));
    TEST_ASSERT_EQUAL_STRING("N0ABC>APRS,WIDE2-1,qAR,ME:>inner", out.c_str());
}

static void test_is_to_rf() {
    gating::IsPacket p;
    TEST_ASSERT_FALSE(gating::splitIsLine("# aprsc 2.1", p));
    TEST_ASSERT_TRUE(gating::splitIsLine("APRSPH>APAIOR,TCPIP*,qAC,T2X::N0CALL-9 :hi{12\r\n", p));
    TEST_ASSERT_EQUAL_STRING("APRSPH", p.src.c_str());
    TEST_ASSERT_EQUAL_STRING("TCPIP*,qAC,T2X", p.path.c_str());
    TEST_ASSERT_EQUAL_STRING(":N0CALL-9 :hi{12", p.info.c_str());
    TEST_ASSERT_EQUAL_STRING("}APRSPH>APAIOR,TCPIP,ME-10*::N0CALL-9 :hi{12", gating::isToRfInfo(p, "ME-10").c_str());
}

static ax25::Frame frame(const char *tnc2) {
    ax25::Frame f;
    TEST_ASSERT_TRUE(ax25::fromTnc2(tnc2, f));
    return f;
}

static void test_digipeat() {
    ax25::Address me;
    ax25::parseAddress("N0CALL-9", me);
    ax25::Frame f = frame("N0ABC>APRS,WIDE1-1,WIDE2-1:>x");
    TEST_ASSERT_TRUE(gating::digipeat(f, me));
    TEST_ASSERT_EQUAL_STRING("N0ABC>APRS,N0CALL-9,WIDE1*,WIDE2-1:>x", ax25::toTnc2(f).c_str());
    // Already used once: not again.
    TEST_ASSERT_FALSE(gating::digipeat(f, me));
    f = frame("N0ABC>APRS,N0CALL-9,WIDE2-1:>x");
    TEST_ASSERT_TRUE(gating::digipeat(f, me));
    TEST_ASSERT_EQUAL_STRING("N0ABC>APRS,N0CALL-9*,WIDE2-1:>x", ax25::toTnc2(f).c_str());
    f = frame("N0ABC>APRS,WIDE2-1:>x");
    TEST_ASSERT_FALSE(gating::digipeat(f, me));
    f = frame("N0ABC>APRS,N1DIG*,WIDE1-1:>x");
    TEST_ASSERT_TRUE(gating::digipeat(f, me));
    f = frame("N0CALL-9>APRS,WIDE1-1:>x");
    TEST_ASSERT_FALSE(gating::digipeat(f, me));
}

static void test_lora_frame() {
    const std::string f = lora_frame::encode("N0CALL-7>APLRT1,WIDE1-1:!4200.00N/08300.00W>");
    TEST_ASSERT_EQUAL_UINT8('<', (uint8_t)f[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, (uint8_t)f[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01, (uint8_t)f[2]);
    std::string out;
    const std::string rx = f + "\r\n";
    TEST_ASSERT_TRUE(lora_frame::decode((const uint8_t *)rx.data(), rx.size(), out));
    TEST_ASSERT_EQUAL_STRING("N0CALL-7>APLRT1,WIDE1-1:!4200.00N/08300.00W>", out.c_str());
    // Prefix-less TNC2 accepted, garbage rejected.
    const char *bare = "N0CALL>APRS::hello";
    TEST_ASSERT_TRUE(lora_frame::decode((const uint8_t *)bare, strlen(bare), out));
    const uint8_t junk[] = {0x12, 0x34, 0x3E, 0x00, 0x3A};
    TEST_ASSERT_FALSE(lora_frame::decode(junk, sizeof(junk), out));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_rf_to_is);
    RUN_TEST(test_is_to_rf);
    RUN_TEST(test_digipeat);
    RUN_TEST(test_lora_frame);
    return UNITY_END();
}
