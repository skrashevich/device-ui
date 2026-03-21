#include "graphics/map/URLService.h"
#include "graphics/map/MapTileSettings.h"
#include "util/ILog.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#ifndef __has_include
#define __has_include(x) 0
#endif

#if __has_include(<HTTPClient.h>) && __has_include(<WiFi.h>) && __has_include(<WiFiClientSecure.h>)
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#define URLSERVICE_HTTP_ENABLED 1
#include "FS.h"
#ifdef ARCH_PORTDUINO
#include "PortduinoFS.h"
static fs::FS &CacheFS = PortduinoFS;
#elif defined(HAS_SD_MMC)
#include "SD_MMC.h"
static auto &CacheFS = SD_MMC;
#elif __has_include(<SD.h>)
#include "SD.h"
static auto &CacheFS = SD;
#define HAS_SD_CACHE 1
#endif
#else
#define URLSERVICE_HTTP_ENABLED 0
#endif

#if __has_include(<esp_task_wdt.h>)
#include <esp_task_wdt.h>
#define URLSERVICE_HAS_WDT 1
#else
#define URLSERVICE_HAS_WDT 0
#endif

namespace {

#if URLSERVICE_HTTP_ENABLED

struct UrlFile {
    std::vector<uint8_t> bytes;
    size_t pos = 0;
};

// Single-slot cache: load() pre-fetches the tile so it can return a
// meaningful bool.  fs_open() consumes it, avoiding a second HTTP request.
struct TilePreFetchCache {
    std::string path;
    std::vector<uint8_t> bytes;
    bool valid = false;

    void store(const char *p, std::vector<uint8_t> &&b)
    {
        path = p;
        bytes = std::move(b);
        valid = true;
    }

    bool consume(const char *p, std::vector<uint8_t> &out)
    {
        if (!valid || path != p) {
            return false;
        }
        out = std::move(bytes);
        valid = false;
        path.clear();
        return true;
    }
};
static TilePreFetchCache tilePreFetchCache;

static const char *const TILE_HOSTS[] = {"http://a.tile.openstreetmap.org", "http://b.tile.openstreetmap.org",
                                         "http://c.tile.openstreetmap.org"};
static constexpr size_t TILE_HOSTS_COUNT = sizeof(TILE_HOSTS) / sizeof(TILE_HOSTS[0]);
static constexpr size_t MAX_TILE_SIZE_BYTES = 64 * 1024;
static constexpr const char *TILE_USER_AGENT = "Meshtastic-DeviceUI/1.0 (+https://meshtastic.org/)";
static constexpr const char *YANDEX_TILE_URL = "https://tiles.api-maps.yandex.ru/v1/tiles/";
static constexpr const char *YANDEX_TILE_API_KEY = "45735e54-8e67-4809-95b6-6cf1e13a4b6b";

#if defined(HAS_SD_CACHE) || defined(HAS_SD_MMC) || defined(ARCH_PORTDUINO)
#define SD_CACHE_ENABLED 1
static bool sdCacheChecked = false;
static bool sdCacheAvail = false;

bool checkSdCache() {
    if (sdCacheChecked) return sdCacheAvail;
    sdCacheChecked = true;
    File root = CacheFS.open("/");
    if (!root) { sdCacheAvail = false; return false; }
    root.close();
    sdCacheAvail = true;
    CacheFS.mkdir("/tiles");
    return true;
}

bool loadFromSdCache(uint32_t z, uint32_t x, uint32_t y, std::vector<uint8_t> &bytes) {
    if (!checkSdCache()) return false;
    char path[64];
    snprintf(path, sizeof(path), "/tiles/%u/%u/%u.png", z, x, y);
    File f = CacheFS.open(path, FILE_READ);
    if (!f) return false;
    size_t sz = f.size();
    if (sz == 0 || sz > MAX_TILE_SIZE_BYTES) { f.close(); return false; }
    bytes.resize(sz);
    size_t rd = f.read(bytes.data(), sz);
    f.close();
    return rd == sz;
}

void saveToSdCache(uint32_t z, uint32_t x, uint32_t y, const std::vector<uint8_t> &bytes) {
    if (!checkSdCache() || bytes.empty()) return;
    char dir1[32], dir2[48], path[64];
    snprintf(dir1, sizeof(dir1), "/tiles/%u", z);
    snprintf(dir2, sizeof(dir2), "/tiles/%u/%u", z, x);
    snprintf(path, sizeof(path), "/tiles/%u/%u/%u.png", z, x, y);
    CacheFS.mkdir(dir1);
    CacheFS.mkdir(dir2);
    File f = CacheFS.open(path, FILE_WRITE);
    if (f) { f.write(bytes.data(), bytes.size()); f.close(); }
}
#endif

bool parseUInt(const char *start, const char *end, uint32_t &value)
{
    if (!start || !end || start >= end) {
        return false;
    }

    uint32_t result = 0;
    for (const char *p = start; p < end; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        uint32_t digit = static_cast<uint32_t>(*p - '0');
        if (result > (std::numeric_limits<uint32_t>::max() - digit) / 10U) {
            return false;
        }
        result = result * 10U + digit;
    }
    value = result;
    return true;
}

bool parseTilePath(const char *path, uint32_t &z, uint32_t &x, uint32_t &y)
{
    if (!path) {
        return false;
    }

    const char *slashY = strrchr(path, '/');
    if (!slashY || slashY[1] == '\0' || slashY <= path) {
        return false;
    }

    const char *dot = strrchr(slashY + 1, '.');
    if (!dot || dot <= slashY + 1) {
        return false;
    }

    const char *slashX = slashY - 1;
    while (slashX > path && *slashX != '/') {
        --slashX;
    }
    if (*slashX != '/' || slashX + 1 >= slashY || slashX <= path) {
        return false;
    }

    const char *slashZ = slashX - 1;
    while (slashZ > path && *slashZ != '/') {
        --slashZ;
    }
    if (*slashZ != '/' || slashZ + 1 >= slashX) {
        return false;
    }

    return parseUInt(slashZ + 1, slashX, z) && parseUInt(slashX + 1, slashY, x) && parseUInt(slashY + 1, dot, y);
}

bool buildTileUrl(uint32_t z, uint32_t x, uint32_t y, char *url, size_t urlSize)
{
    if (!url || urlSize == 0U) {
        return false;
    }

    if (MapTileSettings::getTileProvider() == MapTileProvider::Yandex) {
        const int n = std::snprintf(url, urlSize,
                                    "%s?x=%u&y=%u&z=%u&lang=ru_RU&l=map&apikey=%s",
                                    YANDEX_TILE_URL,
                                    x,
                                    y,
                                    z,
                                    YANDEX_TILE_API_KEY);
        return n > 0 && static_cast<size_t>(n) < urlSize;
    }

    const uint32_t index = (z + x + y) % static_cast<uint32_t>(TILE_HOSTS_COUNT);
    const int n = std::snprintf(url, urlSize, "%s/%u/%u/%u.png", TILE_HOSTS[index], z, x, y);
    return n > 0 && static_cast<size_t>(n) < urlSize;
}

bool fetchTile(const char *path, std::vector<uint8_t> &bytes)
{
    uint32_t z = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    if (!parseTilePath(path, z, x, y)) {
        ILOG_DEBUG("URLService invalid tile path: %s", path ? path : "(null)");
        return false;
    }

    char url[256];
    if (!buildTileUrl(z, x, y, url, sizeof(url))) {
        ILOG_DEBUG("URLService failed to build tile URL");
        return false;
    }

    // Try SD cache first
#if defined(SD_CACHE_ENABLED)
    if (loadFromSdCache(z, x, y, bytes)) {
        ILOG_DEBUG("URLService: tile from SD cache: %u/%u/%u", z, x, y);
        return true;
    }
#endif

    // Temporarily remove this task from the watchdog monitor: the SSL handshake
    // alone can take 6+ seconds, which exceeds the default WDT timeout (5s).
    // Use a RAII-style lambda to guarantee re-registration on every exit path.
#if URLSERVICE_HAS_WDT
    esp_task_wdt_delete(NULL);
    struct WdtGuard {
        ~WdtGuard() { esp_task_wdt_add(NULL); }
    } wdtGuard;
#endif

    static WiFiClient httpClient;
    static WiFiClientSecure httpsClient;
    static bool httpsClientInit = false;

    bool isHttps = (strncmp(url, "https", 5) == 0);
    if (isHttps && !httpsClientInit) {
        httpsClient.setInsecure();
        httpsClientInit = true;
    }
    WiFiClient &activeClient = isHttps ? static_cast<WiFiClient&>(httpsClient) : httpClient;

    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(5000);
    http.setReuse(true);
    http.setUserAgent(TILE_USER_AGENT);

    if (!http.begin(activeClient, url)) {
        ILOG_DEBUG("URLService begin failed: %s", url);
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        ILOG_DEBUG("URLService HTTP code %d for %s", code, url);
        http.end();
        return false;
    }

    int32_t remaining = http.getSize();
    if (remaining > 0 && static_cast<size_t>(remaining) > MAX_TILE_SIZE_BYTES) {
        ILOG_DEBUG("URLService tile too large (%d): %s", remaining, url);
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    if (!stream) {
        http.end();
        return false;
    }

    bytes.clear();
    if (remaining > 0) {
        bytes.reserve(static_cast<size_t>(remaining));
    }

    uint8_t buffer[2048];
    uint32_t idleLoops = 0;
    while ((http.connected() || stream->available() > 0) && (remaining > 0 || remaining == -1)) {
        const int avail = stream->available();
        if (avail <= 0) {
            yield();
            if (++idleLoops > 2000U) {
                break;
            }
            continue;
        }

        idleLoops = 0;
        const size_t available = static_cast<size_t>(avail);
        const size_t chunk = std::min(available, sizeof(buffer));
        const size_t readLen = stream->readBytes(buffer, chunk);
        if (readLen == 0U) {
            break;
        }

        if (bytes.size() + readLen > MAX_TILE_SIZE_BYTES) {
            ILOG_DEBUG("URLService tile exceeded max size: %s", url);
            bytes.clear();
            http.end();
            return false;
        }

        bytes.insert(bytes.end(), buffer, buffer + readLen);

        if (remaining > 0) {
            remaining = std::max<int32_t>(0, remaining - static_cast<int32_t>(readLen));
        }
    }

    http.end();
    if (bytes.empty()) {
        ILOG_DEBUG("URLService empty response: %s", url);
        return false;
    }

#if defined(SD_CACHE_ENABLED)
    saveToSdCache(z, x, y, bytes);
#endif

    return true;
}

#endif

} // namespace

URLService::URLService() : ITileService("U:")
{
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = 'U';
    drv.cache_size = MapTileSettings::getCacheSize();
    drv.ready_cb = nullptr;
    drv.open_cb = fs_open;
    drv.close_cb = fs_close;
    drv.read_cb = fs_read;
    drv.write_cb = fs_write;
    drv.seek_cb = fs_seek;
    drv.tell_cb = fs_tell;
    lv_fs_drv_register(&drv);
}

URLService::~URLService() {}

bool URLService::load(const char *name, void *img)
{
#if URLSERVICE_HTTP_ENABLED
    if (!name || !img || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    char buf[128];
    const int n = std::snprintf(buf, sizeof(buf), "%s%s", idLetter, name);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) {
        return false;
    }

    // Pre-fetch so we can return a meaningful bool and avoid a second HTTP
    // request when LVGL calls fs_open() during the draw phase.
    std::vector<uint8_t> bytes;
    if (!fetchTile(name, bytes)) {
        ILOG_DEBUG("URLService: tile unavailable: %s", name);
        return false;
    }
    tilePreFetchCache.store(name, std::move(bytes));

    // lv_image_set_src schedules a redraw; the actual fs_open call happens
    // during the draw phase and will consume the pre-fetched bytes above.
    lv_image_set_src(static_cast<lv_obj_t *>(img), buf);
    return true;
#else
    (void)name;
    (void)img;
    return false;
#endif
}

void *URLService::fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    (void)drv;
#if URLSERVICE_HTTP_ENABLED
    if (!path || !(mode & LV_FS_MODE_RD) || WiFi.status() != WL_CONNECTED) {
        return nullptr;
    }

    UrlFile *file = new UrlFile;
    // Consume pre-fetched bytes if load() already downloaded this tile.
    if (!tilePreFetchCache.consume(path, file->bytes)) {
        if (!fetchTile(path, file->bytes)) {
            delete file;
            return nullptr;
        }
    }
    file->pos = 0;
    return static_cast<void *>(file);
#else
    (void)path;
    (void)mode;
    return nullptr;
#endif
}

lv_fs_res_t URLService::fs_close(lv_fs_drv_t *drv, void *file_p)
{
    (void)drv;
#if URLSERVICE_HTTP_ENABLED
    if (!file_p) {
        return LV_FS_RES_UNKNOWN;
    }
    delete static_cast<UrlFile *>(file_p);
    return LV_FS_RES_OK;
#else
    (void)file_p;
    return LV_FS_RES_NOT_IMP;
#endif
}

lv_fs_res_t URLService::fs_read(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br)
{
    (void)drv;
#if URLSERVICE_HTTP_ENABLED
    if (!file_p || !buf || !br) {
        return LV_FS_RES_UNKNOWN;
    }

    UrlFile *file = static_cast<UrlFile *>(file_p);
    const size_t remaining = (file->pos < file->bytes.size()) ? (file->bytes.size() - file->pos) : 0U;
    const uint32_t toRead = std::min<uint32_t>(btr, static_cast<uint32_t>(remaining));
    if (toRead > 0U) {
        std::memcpy(buf, file->bytes.data() + file->pos, toRead);
        file->pos += static_cast<size_t>(toRead);
    }

    *br = toRead;
    return LV_FS_RES_OK;
#else
    (void)file_p;
    (void)buf;
    (void)btr;
    (void)br;
    return LV_FS_RES_NOT_IMP;
#endif
}

lv_fs_res_t URLService::fs_write(lv_fs_drv_t *drv, void *file_p, const void *buf, uint32_t btw, uint32_t *bw)
{
    (void)drv;
    (void)file_p;
    (void)buf;
    (void)btw;
    (void)bw;
    return LV_FS_RES_NOT_IMP;
}

lv_fs_res_t URLService::fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence)
{
    (void)drv;
#if URLSERVICE_HTTP_ENABLED
    if (!file_p) {
        return LV_FS_RES_UNKNOWN;
    }

    UrlFile *file = static_cast<UrlFile *>(file_p);
    size_t target = 0;
    switch (whence) {
    case LV_FS_SEEK_SET:
        target = static_cast<size_t>(pos);
        break;
    case LV_FS_SEEK_CUR:
        target = file->pos + static_cast<size_t>(pos);
        break;
    case LV_FS_SEEK_END:
        target = file->bytes.size() + static_cast<size_t>(pos);
        break;
    default:
        return LV_FS_RES_UNKNOWN;
    }

    if (target > file->bytes.size()) {
        return LV_FS_RES_UNKNOWN;
    }

    file->pos = target;
    return LV_FS_RES_OK;
#else
    (void)file_p;
    (void)pos;
    (void)whence;
    return LV_FS_RES_NOT_IMP;
#endif
}

lv_fs_res_t URLService::fs_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
    (void)drv;
#if URLSERVICE_HTTP_ENABLED
    if (!file_p || !pos_p) {
        return LV_FS_RES_UNKNOWN;
    }

    UrlFile *file = static_cast<UrlFile *>(file_p);
    *pos_p = static_cast<uint32_t>(file->pos);
    return LV_FS_RES_OK;
#else
    (void)file_p;
    (void)pos_p;
    return LV_FS_RES_NOT_IMP;
#endif
}
