#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

#include "apps/BerryEngine.h"
#include "apps/AppContext.h"
#include "util/ILog.h"

// Berry C API
#include "berry.h"

BerryEngine::BerryEngine() : vm(nullptr), appCtx(nullptr), initialized(false) {}

BerryEngine::~BerryEngine()
{
    destroy();
}

bool BerryEngine::init(size_t heapSize)
{
    if (initialized)
        return true;

    vm = be_vm_new();
    if (!vm) {
        ILOG_ERROR("BerryEngine: failed to create VM");
        return false;
    }

    be_set_obs_hook(vm, nullptr); // no GC observer
    registerBuiltins();
    initialized = true;
    ILOG_INFO("BerryEngine: VM initialized");
    return true;
}

bool BerryEngine::loadScript(const char *path)
{
    if (!initialized || !vm)
        return false;

    int result = be_loadfile(vm, path);
    if (result != 0) {
        ILOG_ERROR("BerryEngine: failed to load '%s': %s", path, be_tostring(vm, -1));
        be_pop(vm, 1);
        return false;
    }

    result = be_pcall(vm, 0);
    if (result != 0) {
        ILOG_ERROR("BerryEngine: error executing '%s': %s", path, be_tostring(vm, -1));
        be_pop(vm, 1);
        return false;
    }
    be_pop(vm, 1);
    return true;
}

bool BerryEngine::loadString(const char *code)
{
    if (!initialized || !vm)
        return false;

    int result = be_loadbuffer(vm, "script", code, strlen(code));
    if (result != 0) {
        ILOG_ERROR("BerryEngine: compile error: %s", be_tostring(vm, -1));
        be_pop(vm, 1);
        return false;
    }

    result = be_pcall(vm, 0);
    if (result != 0) {
        ILOG_ERROR("BerryEngine: runtime error: %s", be_tostring(vm, -1));
        be_pop(vm, 1);
        return false;
    }
    be_pop(vm, 1);
    return true;
}

bool BerryEngine::callFunction(const char *name)
{
    if (!initialized || !vm)
        return false;

    be_getglobal(vm, name);
    if (!be_isfunction(vm, -1)) {
        be_pop(vm, 1);
        return false;
    }

    int result = be_pcall(vm, 0);
    if (result != 0) {
        ILOG_ERROR("BerryEngine: error calling '%s': %s", name, be_tostring(vm, -1));
    }
    be_pop(vm, 1 + (result == 0 ? 1 : 0));
    return result == 0;
}

bool BerryEngine::callFunction(const char *name, const char *arg)
{
    if (!initialized || !vm)
        return false;

    be_getglobal(vm, name);
    if (!be_isfunction(vm, -1)) {
        be_pop(vm, 1);
        return false;
    }

    be_pushstring(vm, arg);
    int result = be_pcall(vm, 1);
    if (result != 0) {
        ILOG_ERROR("BerryEngine: error calling '%s': %s", name, be_tostring(vm, -1));
    }
    be_pop(vm, 1 + (result == 0 ? 1 : 0));
    return result == 0;
}

bool BerryEngine::callFunction(const char *name, const char *arg1, const char *arg2)
{
    if (!initialized || !vm)
        return false;

    be_getglobal(vm, name);
    if (!be_isfunction(vm, -1)) {
        be_pop(vm, 1);
        return false;
    }

    be_pushstring(vm, arg1);
    be_pushstring(vm, arg2);
    int result = be_pcall(vm, 2);
    if (result != 0) {
        ILOG_ERROR("BerryEngine: error calling '%s': %s", name, be_tostring(vm, -1));
    }
    be_pop(vm, 1 + (result == 0 ? 1 : 0));
    return result == 0;
}

bool BerryEngine::callFunction(const char *name, int arg)
{
    if (!initialized || !vm)
        return false;

    be_getglobal(vm, name);
    if (!be_isfunction(vm, -1)) {
        be_pop(vm, 1);
        return false;
    }

    be_pushint(vm, arg);
    int result = be_pcall(vm, 1);
    if (result != 0) {
        ILOG_ERROR("BerryEngine: error calling '%s': %s", name, be_tostring(vm, -1));
    }
    be_pop(vm, 1 + (result == 0 ? 1 : 0));
    return result == 0;
}

void BerryEngine::registerNativeFunction(const char *name, NativeFunc fn)
{
    // Berry uses a different mechanism for native function registration.
    // For now, built-in modules are registered via registerBuiltins().
    ILOG_WARN("BerryEngine: registerNativeFunction not yet implemented for '%s'", name);
}

void BerryEngine::destroy()
{
    if (vm) {
        be_vm_delete(vm);
        vm = nullptr;
    }
    initialized = false;
}

void BerryEngine::registerBuiltins()
{
    // Built-in modules will be registered here:
    // - mesh: send/receive mesh messages, kv store
    // - ui: LVGL widget creation bindings
    // - http: HTTP GET/POST
    // These are implemented as Berry native modules.
    // For now, this is a placeholder for the module registration.
}

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
