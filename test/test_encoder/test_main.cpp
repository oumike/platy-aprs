#include <unity.h>
#include "aprs/aprs_encoder.h"
#include "aprs/aprs_parser.h"
#include "aprs/smart_beacon.h"
#include "util/geo.h"

void setUp() {}
void tearDown() {}

static void test_position_roundtrip() {
    aprs::PositionReport r;
    r.lat = 42.47133;
    r.lon = -83.43167;
    r.symCode = '>';
    r.hasCourseSpeed = true;
    r.course = 0;
    r.speedKnots = 25.4;
    r.hasAlt = true;
    r.altFeet = 919;
    r.comment = "hi";
    const std::string s = aprs::encodePosition(r);
    TEST_ASSERT_EQUAL_STRING("=4228.28N/08325.90W>360/025/A=000919hi", s.c_str());
    aprs::Packet p;
    TEST_ASSERT_TRUE(aprs::parse("N0CALL", "APZRHN", s, p));
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, 42.47133, p.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, -83.43167, p.lon);
    TEST_ASSERT_TRUE(p.messaging);
    TEST_ASSERT_EQUAL_STRING("hi", p.comment.c_str());
}

static void test_minutes_carry() {
    aprs::PositionReport r;
    r.lat = -10.999999;
    r.lon = 5.0;
    r.messaging = false;
    TEST_ASSERT_EQUAL_STRING("!1100.00S/00500.00E>", aprs::encodePosition(r).c_str());
}

static void test_message_and_ack() {
    TEST_ASSERT_EQUAL_STRING(":KD8ABC-7 :hello there{12", aprs::encodeMessage("KD8ABC-7", "hello| there{", "12").c_str());
    TEST_ASSERT_EQUAL_STRING(":KD8ABC   :ack12", aprs::encodeAck("KD8ABC", "12").c_str());
    aprs::Packet p;
    TEST_ASSERT_TRUE(aprs::parse("N0CALL", "APZRHN", aprs::encodeMessage("KD8ABC", "yo", "AB1"), p));
    TEST_ASSERT_EQUAL_STRING("KD8ABC", p.addressee.c_str());
    TEST_ASSERT_EQUAL_STRING("yo", p.text.c_str());
    TEST_ASSERT_EQUAL_STRING("AB1", p.msgId.c_str());
    TEST_ASSERT_EQUAL(67, (int)aprs::cleanMessageText(std::string(80, 'x')).size());
}

static void test_smart_beacon() {
    aprs::SmartBeaconParams prm;
    aprs::SmartBeacon sb;
    TEST_ASSERT_TRUE(sb.due(0, 0, 0, prm));   // first beacon right away
    sb.sent(100, 90);
    TEST_ASSERT_EQUAL_UINT32(1800, aprs::SmartBeacon::rateFor(2, prm));
    TEST_ASSERT_EQUAL_UINT32(90, aprs::SmartBeacon::rateFor(120, prm));
    TEST_ASSERT_EQUAL_UINT32(180, aprs::SmartBeacon::rateFor(45, prm));
    TEST_ASSERT_FALSE(sb.due(200, 45, 90, prm));
    TEST_ASSERT_TRUE(sb.due(280, 45, 90, prm));
    // 45 km/h = 28 mph: threshold 28 + 255/28 = ~37 degrees.
    TEST_ASSERT_FALSE(sb.due(120, 45, 120, prm));   // 30 degrees
    TEST_ASSERT_TRUE(sb.due(120, 45, 135, prm));    // 45 degrees
    TEST_ASSERT_FALSE(sb.due(110, 45, 180, prm));   // before min turn time
    TEST_ASSERT_FALSE(sb.due(120, 3, 270, prm));    // too slow to corner-peg
}

static void test_web_mercator() {
    // Known tile: Detroit area at z10 lies in tile x=274, y=378 (OSM numbering).
    TEST_ASSERT_EQUAL_INT(274, (int)(geo::lonToWorldX(-83.364, 10) / 256));
    TEST_ASSERT_EQUAL_INT(378, (int)(geo::latToWorldY(42.468, 10) / 256));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 42.468, geo::worldYToLat(geo::latToWorldY(42.468, 14), 14));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -83.364, geo::worldXToLon(geo::lonToWorldX(-83.364, 14), 14));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_position_roundtrip);
    RUN_TEST(test_minutes_carry);
    RUN_TEST(test_message_and_ack);
    RUN_TEST(test_smart_beacon);
    RUN_TEST(test_web_mercator);
    return UNITY_END();
}
