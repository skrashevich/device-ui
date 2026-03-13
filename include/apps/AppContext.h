#pragma once

#ifdef HAS_CUSTOM_APPS

#include <functional>
#include <string>

class ViewController;
class MeshtasticView;

/**
 * Service API available to custom apps.
 * Wraps ViewController and provides mesh, HTTP, and storage access.
 */
class AppContext
{
  public:
    AppContext(ViewController *controller, MeshtasticView *view);

    // --- Mesh messaging ---
    void sendTextMessage(uint32_t to, uint8_t channel, const char *msg);
    void broadcastMessage(uint8_t channel, const char *msg);
    uint32_t getMyNodeNum() const;

    // --- Persistent key-value storage (LittleFS/SD) ---
    bool kvStore(const char *key, const char *value);
    std::string kvLoad(const char *key);

    // --- Accessors ---
    ViewController *getController() { return controller; }
    MeshtasticView *getView() { return view; }

  private:
    ViewController *controller;
    MeshtasticView *view;
};

#endif // HAS_CUSTOM_APPS
