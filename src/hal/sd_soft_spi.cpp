#include "sd_soft_spi.h"
#include "board_pins.h"

#if HAS_SD_CARD && SD_USE_SOFT_SPI

#include <vfs_api.h>
#include "diskio_impl.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "soc/gpio_reg.h"
#include "rom/ets_sys.h"

static_assert(PIN_SD_SCK >= 0 && PIN_SD_SCK < 32, "PIN_SD_SCK must be GPIO0-31");
static_assert(PIN_SD_MISO >= 0 && PIN_SD_MISO < 32, "PIN_SD_MISO must be GPIO0-31");
static_assert(PIN_SD_MOSI >= 0 && PIN_SD_MOSI < 32, "PIN_SD_MOSI must be GPIO0-31");
static_assert(PIN_SD_CS >= 0 && PIN_SD_CS < 32, "PIN_SD_CS must be GPIO0-31");

namespace {

constexpr const char *kMountPoint = "/sdcard";
constexpr uint8_t kMaxOpenFiles = 5;
constexpr uint32_t kSckMask  = 1UL << PIN_SD_SCK;
constexpr uint32_t kMosiMask = 1UL << PIN_SD_MOSI;
constexpr uint32_t kCsMask   = 1UL << PIN_SD_CS;

enum CardType : uint8_t { CT_NONE, CT_MMC, CT_SDSC, CT_SDHC };

// R1 and data-token values from the SD Physical Layer spec, SPI mode.
constexpr uint8_t R1_READY      = 0x00;
constexpr uint8_t R1_IDLE       = 0x01;
constexpr uint8_t R1_ILLEGAL    = 0x04;
constexpr uint8_t TOKEN_SINGLE  = 0xFE;   // read data, single-block write
constexpr uint8_t TOKEN_MULTI   = 0xFC;   // each block of a CMD25 write
constexpr uint8_t TOKEN_STOP    = 0xFD;   // ends a CMD25 write
constexpr uint8_t DATA_ACCEPTED = 0x05;

class SdSoftFS : public fs::FS {
public:
    SdSoftFS() : fs::FS(fs::FSImplPtr(new VFSImpl())) {}
    void setMountpoint(const char *mp) { _impl->mountpoint(mp); }
};

SdSoftFS sFs;
CardType sType = CT_NONE;
uint32_t sSectors = 0;
BYTE sPdrv = FF_DRV_NOT_USED;
bool sMounted = false;
bool sVfsRegistered = false;
// Keep identification below 400 kHz; the data clock is software-driven, not a
// negotiated hardware rate.
bool sSlow = true;

inline void bitDelay() {
    if (sSlow) ets_delay_us(2);
}

inline void csLow() {
    REG_WRITE(GPIO_OUT_W1TC_REG, kCsMask);
}

// Deselect, then clock one byte so the card releases MISO.
inline uint8_t xfer(uint8_t out);
inline void release() {
    REG_WRITE(GPIO_OUT_W1TS_REG, kCsMask);
    xfer(0xFF);
}

// SPI mode 0, MSB first: the card shifts out on the falling edge and samples
// on the rising one.
inline uint8_t xfer(uint8_t out) {
    uint8_t in = 0;
    for (int i = 7; i >= 0; --i) {
        REG_WRITE((out >> i) & 1 ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, kMosiMask);
        bitDelay();
        REG_WRITE(GPIO_OUT_W1TS_REG, kSckMask);
        bitDelay();
        in = (uint8_t)((in << 1) | ((REG_READ(GPIO_IN_REG) >> PIN_SD_MISO) & 1));
        REG_WRITE(GPIO_OUT_W1TC_REG, kSckMask);
    }
    return in;
}

inline uint8_t rx() { return xfer(0xFF); }

bool waitReady(uint32_t timeoutMs) {
    const uint32_t start = millis();
    do {
        if (rx() == 0xFF) return true;
        delay(1);
    } while (millis() - start < timeoutMs);
    return false;
}

uint8_t commandCrc(const uint8_t *frame, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        uint8_t byte = frame[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (uint8_t)(crc << 1);
            if ((byte ^ crc) & 0x80) crc ^= 0x09;
            byte = (uint8_t)(byte << 1);
        }
    }
    return (uint8_t)((crc << 1) | 1);
}

uint16_t dataCrc(uint16_t crc, uint8_t byte) {
    crc ^= (uint16_t)byte << 8;
    for (int bit = 0; bit < 8; ++bit) {
        crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}

// Sends a command frame and returns its R1 (0xFF if the card never answered).
// The caller releases the bus once any trailing response or data is read.
uint8_t command(uint8_t cmd, uint32_t arg) {
    csLow();
    if (cmd != 0 && cmd != 12 && !waitReady(500)) return 0xFF;
    const uint8_t frame[] = {
        (uint8_t)(0x40 | cmd), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
        (uint8_t)(arg >> 8), (uint8_t)arg
    };
    for (uint8_t byte : frame) xfer(byte);
    xfer(commandCrc(frame, sizeof(frame)));
    if (cmd == 12) rx();   // stuff byte after STOP_TRANSMISSION
    uint8_t r = 0xFF;
    for (int i = 0; i < 10; ++i) {
        r = rx();
        if (!(r & 0x80)) break;
    }
    return r;
}

uint8_t appCommand(uint8_t cmd, uint32_t arg) {
    const uint8_t r = command(55, 0);
    if (r > R1_IDLE) return r;
    return command(cmd, arg);
}

bool readBlock(uint8_t *buf, size_t len) {
    const uint32_t start = millis();
    uint8_t token;
    do {
        token = rx();
        if (token == 0xFF) delay(1);
    } while (token == 0xFF && millis() - start < 300);
    if (token != TOKEN_SINGLE) return false;
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        buf[i] = rx();
        crc = dataCrc(crc, buf[i]);
    }
    const uint16_t expected = (uint16_t)rx() << 8;
    return (expected | rx()) == crc;
}

bool writeBlock(const uint8_t *buf, uint8_t token) {
    if (!waitReady(1000)) return false;
    xfer(token);
    uint16_t crc = 0;
    for (size_t i = 0; i < 512; ++i) {
        xfer(buf[i]);
        crc = dataCrc(crc, buf[i]);
    }
    xfer((uint8_t)(crc >> 8));
    xfer((uint8_t)crc);
    const bool accepted = (rx() & 0x1F) == DATA_ACCEPTED;
    // Programming can legitimately take a few hundred ms on a slow card.
    const bool ready = waitReady(1000);
    return accepted && ready;
}

uint32_t sectorsFromCsd(const uint8_t *csd) {
    uint64_t sectors;
    const uint8_t version = csd[0] >> 6;
    if (version == 1) {
        // CSD v2 (SDHC/SDXC): capacity = (C_SIZE + 1) * 512 KiB.
        const uint32_t cSize = ((uint32_t)(csd[7] & 0x3F) << 16)
                             | ((uint32_t)csd[8] << 8) | csd[9];
        sectors = ((uint64_t)cSize + 1) * 1024;
    } else if (version == 0) {
        // CSD v1 (SDSC/MMC). Compute bytes first to avoid a negative shift
        // for a malformed READ_BL_LEN and overflow at the capacity limit.
        const uint32_t readBlLen = csd[5] & 0x0F;
        const uint32_t cSize = ((uint32_t)(csd[6] & 0x03) << 10)
                             | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
        const uint32_t cSizeMult = ((csd[9] & 0x03) << 1) | (csd[10] >> 7);
        sectors = (((uint64_t)cSize + 1) << (cSizeMult + 2 + readBlLen)) / 512;
    } else {
        return 0;
    }
    return sectors <= UINT32_MAX ? (uint32_t)sectors : 0;
}

bool cardInit() {
    digitalWrite(PIN_SD_CS, HIGH);
    pinMode(PIN_SD_CS, OUTPUT);
    pinMode(PIN_SD_SCK, OUTPUT);
    pinMode(PIN_SD_MOSI, OUTPUT);
    pinMode(PIN_SD_MISO, INPUT_PULLUP);
    REG_WRITE(GPIO_OUT_W1TC_REG, kSckMask);
    REG_WRITE(GPIO_OUT_W1TS_REG, kMosiMask);
    sSlow = true;
    sType = CT_NONE;
    sSectors = 0;

    // At least 74 clocks with the card deselected to finish its power-up.
    for (int i = 0; i < 10; ++i) rx();

    uint8_t r = 0xFF;
    for (int i = 0; i < 10 && r != R1_IDLE; ++i) {
        r = command(0, 0);
        release();
        if (r != R1_IDLE) delay(10);
    }
    if (r != R1_IDLE) {
        Serial.printf("[sd] soft SPI: CMD0 failed (R1=0x%02X)\n", r);
        return false;
    }

    r = command(59, 1);
    release();
    if (r == (R1_IDLE | R1_ILLEGAL)) {
        Serial.println("[sd] soft SPI: card does not support write CRC checks");
    } else if (r != R1_IDLE) {
        Serial.printf("[sd] soft SPI: CRC enable failed (R1=0x%02X)\n", r);
        return false;
    }

    CardType type = CT_NONE;
    uint32_t start = millis();
    r = command(8, 0x1AA);
    if (r == R1_IDLE) {
        uint8_t echo[4];
        for (auto &b : echo) b = rx();
        release();
        if (echo[2] != 0x01 || echo[3] != 0xAA) {
            Serial.println("[sd] soft SPI: CMD8 voltage/check pattern mismatch");
            return false;
        }
        do {
            r = appCommand(41, 1UL << 30);   // HCS: host supports SDHC
            release();
            if (r == R1_IDLE) delay(1);
        } while (r == R1_IDLE && millis() - start < 1000);
        if (r != R1_READY) {
            Serial.printf("[sd] soft SPI: ACMD41 failed (R1=0x%02X)\n", r);
            return false;
        }
        r = command(58, 0);
        if (r != R1_READY) {
            release();
            Serial.printf("[sd] soft SPI: CMD58 failed (R1=0x%02X)\n", r);
            return false;
        }
        uint8_t ocr[4];
        for (auto &b : ocr) b = rx();
        release();
        if (!(ocr[0] & 0x80) || !(ocr[1] & 0x10)) {
            Serial.println("[sd] soft SPI: card not ready for 3.3 V operation");
            return false;
        }
        type = (ocr[0] & 0x40) ? CT_SDHC : CT_SDSC;
    } else {
        release();
        if (r != (R1_IDLE | R1_ILLEGAL)) {
            Serial.printf("[sd] soft SPI: CMD8 failed (R1=0x%02X)\n", r);
            return false;
        }
        // v1 SD, or failing that, MMC.
        r = appCommand(41, 0);
        release();
        if (r == R1_IDLE) delay(1);
        if (r <= R1_IDLE) {
            type = CT_SDSC;
            while (r == R1_IDLE && millis() - start < 1000) {
                r = appCommand(41, 0);
                release();
            }
        } else {
            type = CT_MMC;
            start = millis();
            do {
                r = command(1, 0);
                release();
                if (r == R1_IDLE) delay(1);
            } while (r == R1_IDLE && millis() - start < 1000);
        }
        if (r != R1_READY) {
            Serial.printf("[sd] soft SPI: legacy card initialization failed (R1=0x%02X)\n", r);
            return false;
        }
    }

    if (type != CT_SDHC) {
        r = command(16, 512);   // byte-addressed cards: fix the block length
        release();
        if (r != R1_READY) {
            Serial.printf("[sd] soft SPI: CMD16 failed (R1=0x%02X)\n", r);
            return false;
        }
    }

    uint8_t csd[16];
    const bool csdOk = command(9, 0) == R1_READY && readBlock(csd, sizeof(csd));
    release();
    if (!csdOk) {
        Serial.println("[sd] soft SPI: CSD read/CRC failed");
        return false;
    }

    sSectors = sectorsFromCsd(csd);
    if (!sSectors || (type != CT_SDHC && sSectors > UINT32_MAX / 512UL + 1)) {
        Serial.println("[sd] soft SPI: invalid or unsupported card capacity");
        sSectors = 0;
        return false;
    }
    sType = type;
    sSlow = false;
    return true;
}

bool readSectors(uint8_t *buf, uint32_t sector, unsigned count) {
    const uint32_t addr = (sType == CT_SDHC) ? sector : sector * 512;
    bool ok;
    if (count == 1) {
        ok = command(17, addr) == R1_READY && readBlock(buf, 512);
    } else {
        ok = command(18, addr) == R1_READY;
        if (ok) {
            for (unsigned i = 0; ok && i < count; ++i) ok = readBlock(buf + i * 512, 512);
            const bool stopped = command(12, 0) == R1_READY;
            const bool ready = waitReady(500);
            ok = ok && stopped && ready;
        }
    }
    release();
    return ok;
}

bool writeSectors(const uint8_t *buf, uint32_t sector, unsigned count) {
    const uint32_t addr = (sType == CT_SDHC) ? sector : sector * 512;
    bool ok;
    if (count == 1) {
        ok = command(24, addr) == R1_READY && writeBlock(buf, TOKEN_SINGLE);
    } else {
        ok = command(25, addr) == R1_READY;
        if (ok) {
            for (unsigned i = 0; ok && i < count; ++i) ok = writeBlock(buf + i * 512, TOKEN_MULTI);
            // Only an accepted CMD25 enters receive mode and owes a stop token.
            if (waitReady(1000)) {
                xfer(TOKEN_STOP);
                rx();
                const bool ready = waitReady(1000);
                ok = ok && ready;
            } else {
                ok = false;
            }
        }
    }
    release();
    return ok;
}

// ── FatFs diskio ─────────────────────────────────────────────────────────────
DSTATUS diskInit(unsigned char) { return sType == CT_NONE ? STA_NOINIT : 0; }
DSTATUS diskStatus(unsigned char) { return sType == CT_NONE ? STA_NOINIT : 0; }

DRESULT diskRead(unsigned char, unsigned char *buf, uint32_t sector, unsigned count) {
    if (sType == CT_NONE) return RES_NOTRDY;
    if (!buf || !count || sector >= sSectors || count > sSectors - sector) {
        Serial.printf("[sd] soft SPI: invalid read sector=%lu count=%u\n",
                      (unsigned long)sector, count);
        return RES_PARERR;
    }
    if (readSectors(buf, sector, count)) return RES_OK;
    if (readSectors(buf, sector, count)) return RES_OK;
    Serial.printf("[sd] soft SPI: read failed sector=%lu count=%u\n",
                  (unsigned long)sector, count);
    return RES_ERROR;
}

DRESULT diskWrite(unsigned char, const unsigned char *buf, uint32_t sector, unsigned count) {
    if (sType == CT_NONE) return RES_NOTRDY;
    if (!buf || !count || sector >= sSectors || count > sSectors - sector) {
        Serial.printf("[sd] soft SPI: invalid write sector=%lu count=%u\n",
                      (unsigned long)sector, count);
        return RES_PARERR;
    }
    if (writeSectors(buf, sector, count)) return RES_OK;
    if (writeSectors(buf, sector, count)) return RES_OK;
    Serial.printf("[sd] soft SPI: write failed sector=%lu count=%u\n",
                  (unsigned long)sector, count);
    return RES_ERROR;
}

DRESULT diskIoctl(unsigned char, unsigned char cmd, void *buf) {
    if (sType == CT_NONE) return RES_NOTRDY;
    if (cmd != CTRL_SYNC && !buf) return RES_PARERR;
    switch (cmd) {
        case CTRL_SYNC: {
            csLow();
            const bool ok = waitReady(500);
            release();
            return ok ? RES_OK : RES_ERROR;
        }
        case GET_SECTOR_COUNT: *(DWORD *)buf = sSectors; return RES_OK;
        case GET_SECTOR_SIZE:  *(WORD *)buf = 512;       return RES_OK;
        case GET_BLOCK_SIZE:   *(DWORD *)buf = 1;        return RES_OK;
        default:               return RES_PARERR;
    }
}

const ff_diskio_impl_t kDiskImpl = {
    .init = &diskInit,
    .status = &diskStatus,
    .read = &diskRead,
    .write = &diskWrite,
    .ioctl = &diskIoctl,
};

void driveName(char out[3]) {
    out[0] = (char)('0' + sPdrv);
    out[1] = ':';
    out[2] = 0;
}

}  // namespace

bool sdSoftBegin() {
    if (sMounted) return true;
    if (!cardInit()) return false;

    BYTE pdrv = FF_DRV_NOT_USED;
    const esp_err_t driveErr = ff_diskio_get_drive(&pdrv);
    if (driveErr != ESP_OK || pdrv == FF_DRV_NOT_USED) {
        Serial.printf("[sd] soft SPI: no FatFs drive available (%s)\n",
                      esp_err_to_name(driveErr));
        sdSoftEnd();
        return false;
    }
    sPdrv = pdrv;
    ff_diskio_register(sPdrv, &kDiskImpl);

    char drv[3];
    driveName(drv);
    FATFS *fs = nullptr;
    const esp_err_t vfsErr = esp_vfs_fat_register(kMountPoint, drv, kMaxOpenFiles, &fs);
    if (vfsErr != ESP_OK) {
        Serial.printf("[sd] soft SPI: VFS registration failed (%s)\n",
                      esp_err_to_name(vfsErr));
        sdSoftEnd();
        return false;
    }
    sVfsRegistered = true;
    // No format on failure: an unreadable card may hold someone's data.
    const FRESULT mountErr = f_mount(fs, drv, 1);
    if (mountErr != FR_OK) {
        Serial.printf("[sd] soft SPI: FAT mount failed (%u); card left unchanged\n",
                      (unsigned)mountErr);
        sdSoftEnd();
        return false;
    }
    sFs.setMountpoint(kMountPoint);
    sMounted = true;
    return true;
}

void sdSoftEnd() {
    sFs.setMountpoint(nullptr);
    if (sVfsRegistered) {
        char drv[3];
        driveName(drv);
        // Detach FatFs before unregistering VFS, which frees its FATFS object.
        const FRESULT err = f_mount(nullptr, drv, 0);
        if (err != FR_OK) {
            Serial.printf("[sd] soft SPI: FAT unmount failed (%u)\n", (unsigned)err);
        }
        const esp_err_t vfsErr = esp_vfs_fat_unregister_path(kMountPoint);
        if (vfsErr != ESP_OK) {
            Serial.printf("[sd] soft SPI: VFS unregister failed (%s)\n",
                          esp_err_to_name(vfsErr));
        }
    }
    if (sPdrv != FF_DRV_NOT_USED) ff_diskio_register(sPdrv, nullptr);
    sPdrv = FF_DRV_NOT_USED;
    sType = CT_NONE;
    sSectors = 0;
    sMounted = false;
    sVfsRegistered = false;
}

bool sdSoftMounted() { return sMounted; }

fs::FS &sdSoftFs() { return sFs; }

uint64_t sdSoftCardSize() {
    return sdSoftMounted() ? (uint64_t)sSectors * 512ULL : 0;
}

uint64_t sdSoftUsedBytes() {
    if (!sMounted) return 0;
    char drv[3];
    driveName(drv);
    FATFS *fs = nullptr;
    DWORD freeClusters = 0;
    if (f_getfree(drv, &freeClusters, &fs) != FR_OK || !fs) return 0;
    const uint64_t clusterBytes = (uint64_t)fs->csize * 512ULL;
    return (uint64_t)(fs->n_fatent - 2) * clusterBytes - (uint64_t)freeClusters * clusterBytes;
}

const char *sdSoftCardTypeName() {
    if (!sdSoftMounted()) return "";
    switch (sType) {
        case CT_SDHC: return "SDHC";
        case CT_SDSC: return "SDSC";
        case CT_MMC:  return "MMC";
        default:      return "";
    }
}

#endif  // SD_USE_SOFT_SPI
