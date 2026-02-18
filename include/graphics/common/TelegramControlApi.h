#pragma once

#include <stdint.h>
#include <string>

#if defined(__has_include)
#if __has_include("telegram/TelegramBridge.h")
#include "telegram/TelegramBridge.h"
#define DEVICE_UI_HAS_TELEGRAM_CONTROL_API 1
#endif
#endif

#ifndef DEVICE_UI_HAS_TELEGRAM_CONTROL_API
#define DEVICE_UI_HAS_TELEGRAM_CONTROL_API 0

enum class TelegramControlSource : uint8_t {
    UNKNOWN = 0,
    DEVICE_UI = 1,
    TELEGRAM_CHAT = 2,
    HTTP_API = 3,
    SERIAL_API = 4,
    OTHER = 255,
};

enum class TelegramControlError : uint8_t {
    NONE = 0,
    NOT_AVAILABLE = 1,
    INVALID_ARGUMENT = 2,
    PERSISTENCE_ERROR = 3,
};

enum class TelegramDirectionMode : uint8_t {
    BOTH = 0,
    MESH_TO_TELEGRAM = 1,
    TELEGRAM_TO_MESH = 2,
};

struct TelegramControlPatch {
    bool hasEnabled = false;
    bool enabled = false;

    bool hasToken = false;
    std::string token;

    bool hasChatId = false;
    std::string chatId;

    bool hasChannels = false;
    std::string channels;

    bool hasPollIntervalMs = false;
    uint32_t pollIntervalMs = 0;

    bool hasLongPollTimeoutSec = false;
    uint32_t longPollTimeoutSec = 0;

    bool hasSendIntervalMs = false;
    uint32_t sendIntervalMs = 0;

    bool hasDirectionMode = false;
    TelegramDirectionMode directionMode = TelegramDirectionMode::BOTH;
};

struct TelegramControlSnapshot {
    bool featureAvailable = false;
    bool enabled = false;
    bool running = false;
    bool configured = false;
    bool wifiConnected = false;

    bool allowAllChannels = true;
    std::string channels;
    uint8_t meshChannelForInject = 0;

    uint16_t queueUsed = 0;
    uint16_t queueCapacity = 0;

    uint32_t pollIntervalMs = 0;
    uint32_t longPollTimeoutSec = 0;
    uint32_t sendIntervalMs = 0;

    TelegramDirectionMode directionMode = TelegramDirectionMode::BOTH;
    bool meshToTelegramEnabled = true;
    bool telegramToMeshEnabled = true;

    bool hasToken = false;
    bool hasChatId = false;
    std::string chatId;
};

struct TelegramControlResult {
    TelegramControlError error = TelegramControlError::NONE;
    bool changed = false;
    bool persisted = false;
    std::string message;

    bool ok() const { return error == TelegramControlError::NONE; }
};

inline TelegramControlSnapshot telegramGetControlSnapshot()
{
    TelegramControlSnapshot snapshot;
    snapshot.featureAvailable = false;
    return snapshot;
}

inline TelegramControlResult telegramApplyControlPatch(const TelegramControlPatch &, TelegramControlSource)
{
    TelegramControlResult result;
    result.error = TelegramControlError::NOT_AVAILABLE;
    result.message = "Telegram bridge is not available in this build";
    return result;
}

inline TelegramControlResult telegramSetEnabled(bool, TelegramControlSource)
{
    TelegramControlResult result;
    result.error = TelegramControlError::NOT_AVAILABLE;
    result.message = "Telegram bridge is not available in this build";
    return result;
}

#endif
