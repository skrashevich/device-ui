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
#else
#define URLSERVICE_HTTP_ENABLED 0
#endif

namespace {

#if URLSERVICE_HTTP_ENABLED

struct UrlFile {
    std::vector<uint8_t> bytes;
    size_t pos = 0;
};

static const char *const TILE_HOSTS[] = {"https://a.tile.openstreetmap.org", "https://b.tile.openstreetmap.org",
                                         "https://c.tile.openstreetmap.org"};
static constexpr size_t TILE_HOSTS_COUNT = sizeof(TILE_HOSTS) / sizeof(TILE_HOSTS[0]);
static constexpr size_t MAX_TILE_SIZE_BYTES = 512 * 1024;
static constexpr const char *OSM_USER_AGENT = "Meshtastic-DeviceUI/1.0 (+https://meshtastic.org/)";

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

bool fetchTile(const char *path, std::vector<uint8_t> &bytes)
{
    uint32_t z = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    if (!parseTilePath(path, z, x, y)) {
        ILOG_DEBUG("URLService invalid tile path: %s", path ? path : "(null)");
        return false;
    }

    const uint32_t index = (z + x + y) % static_cast<uint32_t>(TILE_HOSTS_COUNT);
    char url[160];
    std::snprintf(url, sizeof(url), "%s/%u/%u/%u.png", TILE_HOSTS[index], z, x, y);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(7000);
    http.setReuse(false);
    http.setUserAgent(OSM_USER_AGENT);

    if (!http.begin(client, url)) {
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

    uint8_t buffer[1024];
    uint32_t idleLoops = 0;
    while ((http.connected() || stream->available() > 0) && (remaining > 0 || remaining == -1)) {
        const int avail = stream->available();
        if (avail <= 0) {
            delay(1);
            if (++idleLoops > 5000U) {
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

        const size_t oldSize = bytes.size();
        bytes.resize(oldSize + readLen);
        std::memcpy(bytes.data() + oldSize, buffer, readLen);

        if (remaining > 0) {
            remaining = std::max<int32_t>(0, remaining - static_cast<int32_t>(readLen));
        }
    }

    http.end();
    if (bytes.empty()) {
        ILOG_DEBUG("URLService empty response: %s", url);
        return false;
    }

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

    lv_image_set_src(static_cast<lv_obj_t *>(img), buf);
    if (!lv_image_get_src(static_cast<lv_obj_t *>(img))) {
        ILOG_DEBUG("Failed to load tile %s from WLAN", buf);
        return false;
    }
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
    if (!fetchTile(path, file->bytes)) {
        delete file;
        return nullptr;
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
