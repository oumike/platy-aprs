#include <unity.h>
#include "kiss/kiss.h"

void setUp() {}
void tearDown() {}

static void test_roundtrip_with_escapes() {
    const uint8_t payload[] = {0x01, kiss::FEND, 0x02, kiss::FESC, 0x03};
    auto enc = kiss::encodeData(payload, sizeof(payload), 0);
    const uint8_t expected[] = {0xC0, 0x00, 0x01, 0xDB, 0xDC, 0x02, 0xDB, 0xDD, 0x03, 0xC0};
    TEST_ASSERT_EQUAL_UINT(sizeof(expected), enc.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, enc.data(), sizeof(expected));

    std::vector<uint8_t> got;
    int frames = 0;
    kiss::Decoder d([&](uint8_t port, uint8_t cmd, const uint8_t *data, size_t len) {
        frames++;
        TEST_ASSERT_EQUAL_UINT8(0, port);
        TEST_ASSERT_EQUAL_UINT8(0, cmd);
        got.assign(data, data + len);
    });
    // Feed byte-by-byte, as BLE notifications can split anywhere.
    for (uint8_t b : enc) d.feed(&b, 1);
    TEST_ASSERT_EQUAL_INT(1, frames);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, got.data(), sizeof(payload));
}

static void test_back_to_back_frames_and_empty_frames() {
    const uint8_t stream[] = {0xC0, 0xC0, 0x10, 0xAA, 0xC0, 0x00, 0xBB, 0xCC, 0xC0};
    int frames = 0;
    uint8_t lastPort = 0xFF;
    kiss::Decoder d([&](uint8_t port, uint8_t, const uint8_t *, size_t) {
        frames++;
        lastPort = port;
    });
    d.feed(stream, sizeof(stream));
    TEST_ASSERT_EQUAL_INT(2, frames);
    TEST_ASSERT_EQUAL_UINT8(0, lastPort);
}

static void test_overflow_dropped() {
    int frames = 0;
    kiss::Decoder d([&](uint8_t, uint8_t, const uint8_t *, size_t) { frames++; }, 8);
    const uint8_t big[] = {0xC0, 0x00, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0xC0, 0x00, 1, 0xC0};
    d.feed(big, sizeof(big));
    TEST_ASSERT_EQUAL_INT(1, frames);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_with_escapes);
    RUN_TEST(test_back_to_back_frames_and_empty_frames);
    RUN_TEST(test_overflow_dropped);
    return UNITY_END();
}
