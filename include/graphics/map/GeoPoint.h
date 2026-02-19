#pragma once

#include "Arduino.h"
#include "graphics/map/MapTileSettings.h"
#include "util/ILog.h"
#include <algorithm>
#include <cmath>
#include <stdint.h>

#ifdef HIGH_PRECISION
#define FLOATING_POINT double
#else
#define FLOATING_POINT float
#endif

#ifdef ARCH_PORTDUINO
#ifndef PI
#define PI M_PIl
#endif
#endif

/**
 * Geographical coordinate for map raster tiles.
 * Supports OSM spherical mercator and Yandex wgs84 mercator projection.
 */
class GeoPoint
{
  public:
    GeoPoint(uint32_t xtile, uint32_t ytile, uint8_t zoom)
        : xPos(0), yPos(0), xTile(xtile), yTile(ytile), zoomLevel(zoom), projectionProvider(MapTileSettings::getTileProvider())
    {
        // not used in any scenario yet; so comment out for now
        // reverse calculate from tile x/y/z back to lat/lon (upper left corner 0/0)
        // auto n = 1 << zoom;
        // longitude = FLOATING_POINT(xTile) / n * FLOATING_POINT(360.0) - FLOATING_POINT(180.0);
        // latitude = FLOATING_POINT(180.0) / FLOATING_POINT(PI) *
        //           FLOATING_POINT(std::atan(std::sinh(FLOATING_POINT(PI) * (1.0 - 2.0 * FLOATING_POINT(yTile) / n))));
    }

    GeoPoint(float lat, float lon, uint8_t zoom)
        : latitude(lat), longitude(lon), zoomLevel(255), projectionProvider(MapTileProvider::OSM)
    {
        setZoom(zoom);
    }

    void setZoom(uint8_t zoom)
    {
        const MapTileProvider provider = MapTileSettings::getTileProvider();
        if (zoom == zoomLevel && provider == projectionProvider)
            return;
        projectionProvider = provider;

        // calculate tile x/y/z and xPos/yPos from lat/long
        auto n = 1U << zoom;
        auto size = MapTileSettings::getTileSize();
        auto latitudeClamped = std::max(FLOATING_POINT(-85.05112878), std::min(FLOATING_POINT(latitude), FLOATING_POINT(85.05112878)));
        auto lat_rad = latitudeClamped * FLOATING_POINT(PI) / FLOATING_POINT(180.0);
        auto xRaw = (longitude + 180.0) / 360.0 * n;
        FLOATING_POINT yRaw;
        if (projectionProvider == MapTileProvider::Yandex) {
            constexpr FLOATING_POINT e = FLOATING_POINT(0.0818191908426);
            auto phi = (1.0 - e * std::sin(lat_rad)) / (1.0 + e * std::sin(lat_rad));
            auto theta = std::tan(FLOATING_POINT(PI) / 4.0 + lat_rad / 2.0) * std::pow(phi, e / 2.0);
            yRaw = (1.0 - std::log(theta) / FLOATING_POINT(PI)) / 2.0 * n;
        } else {
            yRaw = (1.0 - std::log(std::tan(lat_rad) + (1.0 / std::cos(lat_rad))) / FLOATING_POINT(PI)) / 2.0 * n;
        }

        yRaw = std::max(FLOATING_POINT(0.0), std::min(yRaw, FLOATING_POINT(n - 1e-6)));
        xRaw = std::max(FLOATING_POINT(0.0), std::min(FLOATING_POINT(xRaw), FLOATING_POINT(n - 1e-6)));
        xPos = uint16_t(xRaw * size) % size;
        yPos = uint16_t(yRaw * size) % size;
        xTile = uint32_t(xRaw);
        yTile = uint32_t(yRaw);
        zoomLevel = zoom;
        // ILOG_DEBUG("zoomed GeoPoint(%f, %f) %d/%d/%d (%d/%d)", latitude, longitude, zoom, xTile, yTile, xPos, yPos);
    }

    // move the GeoPoint position by pixels and recalculate the resulting tile and new lat/lon
    void move(int16_t scrollX, int16_t scrollY)
    {
        // ILOG_DEBUG("move: tile offset: x: %d->%d, y: %d->%d pixel", xPos, xPos - scrollX, yPos, yPos - scrollY);
        auto size = MapTileSettings::getTileSize();
        xPos -= scrollX;
        yPos -= scrollY;
        if (xPos < 0) {
            xTile--;
            xPos += size;
        } else if (xPos >= size) {
            xTile++;
            xPos -= size;
        }
        if (yPos < 0) {
            yTile--;
            yPos += size;
        } else if (yPos >= size) {
            yTile++;
            yPos -= size;
        }

        auto n = 1U << zoomLevel;
        FLOATING_POINT xNorm = (FLOATING_POINT(xTile) + FLOATING_POINT(xPos) / FLOATING_POINT(size)) / FLOATING_POINT(n);
        FLOATING_POINT yNorm = (FLOATING_POINT(yTile) + FLOATING_POINT(yPos) / FLOATING_POINT(size)) / FLOATING_POINT(n);
        xNorm = std::max(FLOATING_POINT(0.0), std::min(xNorm, FLOATING_POINT(1.0)));
        yNorm = std::max(FLOATING_POINT(0.0), std::min(yNorm, FLOATING_POINT(1.0)));
        longitude = xNorm * FLOATING_POINT(360.0) - FLOATING_POINT(180.0);

        if (projectionProvider == MapTileProvider::Yandex) {
            constexpr FLOATING_POINT e = FLOATING_POINT(0.0818191908426);
            auto psi = FLOATING_POINT(PI) * (FLOATING_POINT(1.0) - FLOATING_POINT(2.0) * yNorm);
            auto latRad = FLOATING_POINT(2.0) * std::atan(std::exp(psi)) - FLOATING_POINT(PI) / FLOATING_POINT(2.0);
            for (int i = 0; i < 5; i++) {
                auto sinLat = std::sin(latRad);
                auto ratio = (FLOATING_POINT(1.0) + e * sinLat) / (FLOATING_POINT(1.0) - e * sinLat);
                latRad = FLOATING_POINT(2.0) * std::atan(std::exp(psi) * std::pow(ratio, e / FLOATING_POINT(2.0))) -
                         FLOATING_POINT(PI) / FLOATING_POINT(2.0);
            }
            latitude = latRad * FLOATING_POINT(180.0) / FLOATING_POINT(PI);
        } else {
            latitude = FLOATING_POINT(180.0) / FLOATING_POINT(PI) *
                       FLOATING_POINT(std::atan(std::sinh(FLOATING_POINT(PI) * (1.0 - 2.0 * yNorm))));
        }

        latitude = std::max(FLOATING_POINT(-85.05112878), std::min(FLOATING_POINT(latitude), FLOATING_POINT(85.05112878)));
    }

    // geographical coordinate
    float latitude;
    float longitude;
    // relative pixel position in tile
    int16_t xPos;
    int16_t yPos;
    // OSMtile raster index (inverse mercator projection)
    uint32_t xTile;
    uint32_t yTile;
    // level 0 (course) .. 18 (detail)
    uint8_t zoomLevel;
    MapTileProvider projectionProvider;
    bool isFiltered = false;
    bool isVisible = false;
};

#ifdef UNIT_TEST
#include <doctest/doctest.h>

TEST_CASE("GeoPoint create000Tile")
{
    GeoPoint p(0U, 0U, 0);
    CHECK(p.zoomLevel == 0);
    CHECK(p.xTile == 0);
    CHECK(p.yTile == 0);
}

TEST_CASE("GeoPoint create000Location")
{
    GeoPoint p(0.0f, 0.0f, 0);
    CHECK(p.zoomLevel == 0);
    CHECK(p.xTile == 0);
    CHECK(p.yTile == 0);
}

TEST_CASE("GeoPoint locationMunichFrauenkirche")
{
    GeoPoint p(48.13867316206941f, 11.573006651462567f, 15);
    CHECK(p.zoomLevel == 15);
    CHECK(p.xTile == 17437);
    CHECK(p.yTile == 11371);
}

// TEST_CASE("GeoPoint reverseMunichFrauenkirche") {
//     GeoPoint p(17437U, 11371, 15);
//     CHECK(p.latitude == doctest::Approx(48.1440964f));
//     CHECK(p.longitude == doctest::Approx(11.5686035f));
// }

TEST_CASE("GeoPoint locationSanFrancisco")
{
    GeoPoint p(37.7749f, -122.4194f, 10);
    CHECK(p.zoomLevel == 10);
    CHECK(p.xTile == 163);
    CHECK(p.yTile == 395);
}

TEST_CASE("GeoPoint setZoom")
{
    GeoPoint point(37.7749f, -122.4194f, 10);
    point.setZoom(12);
    CHECK(point.zoomLevel == 12);
}

TEST_CASE("GeoPoint move")
{
    GeoPoint point(37.7749f, -122.4194f, 10);
    int16_t initialXPos = point.xPos;
    int16_t initialYPos = point.yPos;
    point.move(10, 20);
    CHECK(point.xPos == initialXPos - 10);
    CHECK(point.yPos == initialYPos - 20);
}
#endif
