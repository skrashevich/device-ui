#pragma once

#ifdef HAS_CUSTOM_APPS

#include <stdint.h>
#include <string>
#include "lvgl.h"

class ViewController;
class MeshtasticView;

/**
 * @brief Service API available to custom apps.
 * Provides safe access to mesh messaging and persistent storage.
 */
class AppContext
{
  public:
    AppContext(ViewController *ctrl, MeshtasticView *view);

    /// Send a text message to a specific node
    void sendTextMessage(uint32_t to, uint8_t channel, const char *msg);

    /// Broadcast a text message on a channel
    void broadcastMessage(uint8_t channel, const char *msg);

    /// Get our own node number
    uint32_t getMyNodeNum() const;

    /// Store a key-value pair persistently
    bool kvStore(const char *key, const char *value);

    /// Load a value by key from persistent storage
    std::string kvLoad(const char *key);

    /// Request on-screen keyboard for a textarea
    typedef void (*KeyboardRequestFn)(lv_obj_t *textarea, void *ctx);
    void setKeyboardRequestFn(KeyboardRequestFn fn, void *ctx);
    void requestKeyboard(lv_obj_t *textarea);

    /// Access to controller (for advanced use)
    ViewController *getController() { return controller; }

    /// Access to view (for advanced use)
    MeshtasticView *getView() { return view; }

  private:
    ViewController *controller;
    MeshtasticView *view;
    uint32_t myNodeNum = 0;
    KeyboardRequestFn kbRequestFn = nullptr;
    void *kbRequestCtx = nullptr;
};

#endif // HAS_CUSTOM_APPS
