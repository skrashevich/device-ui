#pragma once

#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

#include "apps/ScriptEngine.h"

// Forward declare Berry VM type
struct bvm;

class AppContext;

/**
 * Berry scripting engine implementation.
 * Berry is a lightweight Python-like language proven on ESP32 (Tasmota).
 * ~100KB flash, ~10KB RAM.
 */
class BerryEngine : public ScriptEngine
{
  public:
    BerryEngine();
    ~BerryEngine() override;

    bool init(size_t heapSize) override;
    bool loadScript(const char *path) override;
    bool loadString(const char *code) override;
    bool callFunction(const char *name) override;
    bool callFunction(const char *name, const char *arg) override;
    bool callFunction(const char *name, const char *arg1, const char *arg2) override;
    bool callFunction(const char *name, int arg) override;
    void registerNativeFunction(const char *name, NativeFunc fn) override;
    const char *getEngineName() const override { return "Berry"; }
    void destroy() override;

    /** Register app context for use by native bindings. */
    void setAppContext(AppContext *ctx) { appCtx = ctx; }
    AppContext *getAppContext() const { return appCtx; }

    /** Get the Berry VM (for advanced use by native bindings). */
    bvm *getVM() const { return vm; }

  private:
    bvm *vm;
    AppContext *appCtx;
    bool initialized;

    /** Register built-in native modules (mesh, http, ui). */
    void registerBuiltins();
};

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
