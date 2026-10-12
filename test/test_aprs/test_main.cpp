#include <unity.h>
#include "aprs/aprs_parser.h"

void setUp() {}
void tearDown() {}

static void test_uncompressed_position() {
    aprs::Packet p;
    TEST_ASSERT_TRUE(aprs::parse("N0CALL", "APRS", "!4903.50N/07201.75W-Test 001234", p));
    TEST_ASSERT_EQUAL(aprs::Type::Position, p.type);
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 49.058333, p.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, -72.029167, p.lon);
    TEST_ASSERT_EQUAL_CHAR('/', p.symTable);
    TEST_ASSERT_EQUAL_CHAR('-', p.symCode);
    TEST_ASSERT_EQUAL_STRING("Test 001234", p.comment.c_str());
    TEST_ASSERT_FALSE(p.messaging);
}

static void test_trailing_cr_trimmed() {
    aprs::Packet p;
    TEST_ASSERT_TRUE(aprs::parse("N0CALL", "APRS", "!4903.50N/07201.75W-Test \r", p));
    TEST_ASSERT_EQUAL_STRING("Test", p.comment.c_str());
}

static void test_timestamp_course_speed_altitude() {
    aprs::Packet p;
    TEST_ASSERT_TRUE(aprs::parse("N0CALL", "APRS", "@092345z4903.50N/07201.75W>088/036/A=001234hello", p));
    TEST_ASSERT_TRUE(p.messaging);
    TEST_ASSERT_TRUE(p.hasCourseSpeed);
    TEST_ASSERT_EQUAL_INT(88, p.course);
    TEST_ASSERT_EQUAL_FLOAT(36, p.speedKnots);
    TEST_ASSERT_TRUE(p.hasAlt);
    TEST_ASSERT_EQUAL_FLOAT(1234, p.altFeet);
    TEST_ASSERT_EQUAL_STRING("hello", p.comment.c_str());
}

static void test_compressed_position() {
    aprs::Packet p;
    TEST_ASSERT_TRUE(aprs::parse("N0CALL", "APRS", "=/5L!!<*e7>7P[", p));
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, 49.5, p.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, -72.75, p.lon);
    TEST_ASSERT_EQUAL_CHAR('>', p.symCode);
    TEST_ASSERT_EQUAL_INT(88, p.course);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 36.2, p.speedKnots);
}

static void test_mic_e() {
    // Dest digits 3,3,2,5,6,4 with N + lon-offset + W flags: 33 25.64N 112 07.74W, 20 kt, course 251, jeep.
    aprs::Packet p;
    TEST_ASSERT_TRUE(aprs::parse("N0CALL", "S32UVT", "`(_fn\"Oj/", p));
    TEST_ASSERT_EQUAL(aprs::Type::MicE, p.type);
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 33.427333, p.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, -112.129, p.lon);
    TEST_ASSERT_EQUAL_FLOAT(20, p.speedKnots);
    TEST_ASSERT_EQUAL_INT(251, p.course);
    TEST_ASSERT_EQUAL_CHAR('j', p.symCode);
    TEST_ASSERT_EQUAL_CHAR('/', p.symTable);
}

static void test_message_with_id_and_ack() {
    aprs::Packet p;
    TEST_ASSERT_TRUE(aprs::parse("N0CALL", "APRS", ":KD9ABC-7 :Hello there{42", p));
    TEST_ASSERT_EQUAL(aprs::Type::Message, p.type);
    TEST_ASSERT_EQUAL_STRING("KD9ABC-7", p.addressee.c_str());
    TEST_ASSERT_EQUAL_STRING("Hello there", p.text.c_str());
    TEST_ASSERT_EQUAL_STRING("42", p.msgId.c_str());

    TEST_ASSERT_TRUE(aprs::parse("KD9ABC-7", "APRS", ":N0CALL   :ack42", p));
    TEST_ASSERT_TRUE(p.isAck);
    TEST_ASSERT_EQUAL_STRING("42", p.msgId.c_str());

    // Reply-ack form
    TEST_ASSERT_TRUE(aprs::parse("KD9ABC-7", "APRS", ":N0CALL   :Hi{AB}CD", p));
    TEST_ASSERT_EQUAL_STRING("AB", p.msgId.c_str());
    TEST_ASSERT_EQUAL_STRING("Hi", p.text.c_str());
}

static void test_status_object_item_weather() {
    aprs::Packet p;
    TEST_ASSERT_TRUE(aprs::parse("N0CALL", "APRS", ">On the air", p));
    TEST_ASSERT_EQUAL_STRING("On the air", p.status.c_str());

    TEST_ASSERT_TRUE(aprs::parse("N0CALL", "APRS", ";LEADER   *092345z4903.50N/07201.75W>088/036", p));
    TEST_ASSERT_EQUAL(aprs::Type::Object, p.type);
    TEST_ASSERT_EQUAL_STRING("LEADER", p.objectName.c_str());
    TEST_ASSERT_TRUE(p.objectAlive);
    TEST_ASSERT_TRUE(p.hasPos);

    TEST_ASSERT_TRUE(aprs::parse("N0CALL", "APRS", ")AID #2!4903.50N/07201.75WA", p));
    TEST_ASSERT_EQUAL(aprs::Type::Item, p.type);
    TEST_ASSERT_EQUAL_STRING("AID #2", p.objectName.c_str());

    TEST_ASSERT_TRUE(aprs::parse("N0CALL", "APRS", "!4903.50N/07201.75W_220/004g005t077r000p000P000h50b09900", p));
    TEST_ASSERT_EQUAL(aprs::Type::Weather, p.type);
    TEST_ASSERT_TRUE(p.hasTempF);
    TEST_ASSERT_EQUAL_INT(77, p.tempF);
    TEST_ASSERT_EQUAL_INT(220, p.windDir);
    TEST_ASSERT_EQUAL_INT(4, p.windMph);
    TEST_ASSERT_EQUAL_INT(50, p.humidity);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 990.0, p.pressureMbar);
}

static void test_third_party() {
    aprs::Packet p;
    TEST_ASSERT_TRUE(aprs::parse("IGATE", "APRS", "}KD9ABC>APRS,TCPIP,IGATE*:>via internet", p));
    TEST_ASSERT_TRUE(p.thirdParty);
    TEST_ASSERT_EQUAL_STRING("KD9ABC", p.source.c_str());
    TEST_ASSERT_EQUAL_STRING("via internet", p.status.c_str());
}

static void test_garbage_does_not_crash() {
    aprs::Packet p;
    const char *junk[] = {"", "!", "@123", "`ab", ":short", ";X", ")A", "!9999.99N/99999.99W>", "=/!!!"};
    for (const char *j : junk) aprs::parse("N0CALL", "APRS", j, p);
    TEST_ASSERT_FALSE(aprs::parse("N0CALL", "APRS", "!9999.99N/99999.99W>", p));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_uncompressed_position);
    RUN_TEST(test_trailing_cr_trimmed);
    RUN_TEST(test_timestamp_course_speed_altitude);
    RUN_TEST(test_compressed_position);
    RUN_TEST(test_mic_e);
    RUN_TEST(test_message_with_id_and_ack);
    RUN_TEST(test_status_object_item_weather);
    RUN_TEST(test_third_party);
    RUN_TEST(test_garbage_does_not_crash);
    return UNITY_END();
}
