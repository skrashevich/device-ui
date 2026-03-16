#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

#include "apps/BerryEngine.h"
#include "apps/AppContext.h"
#include "util/ILog.h"

// Berry C API
#include "berry.h"

#if __has_include(<HTTPClient.h>)
#include <HTTPClient.h>
#define HAS_HTTP_CLIENT 1
#endif

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

// ---------------------------------------------------------------------------
// Berry native function helpers
// Each native function receives the VM pointer and returns int (1 = has return value).
// The engine pointer is stored as a Berry global "_eng" (light userdata).
// ---------------------------------------------------------------------------

static BerryEngine *getEngine(bvm *vm)
{
    be_getglobal(vm, "_eng");
    BerryEngine *eng = (BerryEngine *)be_tocomptr(vm, -1);
    be_pop(vm, 1);
    return eng;
}

// ---------------------------------------------------------------------------
// mesh.send(to_node, channel, text) -> nil
// ---------------------------------------------------------------------------
static int be_mesh_send(bvm *vm)
{
    int argc = be_top(vm);
    if (argc < 3) {
        be_return_nil(vm);
    }
    BerryEngine *eng = getEngine(vm);
    AppContext *ctx = eng ? eng->getAppContext() : nullptr;
    if (ctx) {
        uint32_t to = (uint32_t)be_toint(vm, 1);
        uint8_t channel = (uint8_t)be_toint(vm, 2);
        const char *text = be_tostring(vm, 3);
        if (text)
            ctx->sendTextMessage(to, channel, text);
    } else {
        ILOG_ERROR("mesh.send: no AppContext");
    }
    be_return_nil(vm);
}

// ---------------------------------------------------------------------------
// mesh.broadcast(channel, text) -> nil
// ---------------------------------------------------------------------------
static int be_mesh_broadcast(bvm *vm)
{
    int argc = be_top(vm);
    if (argc < 2) {
        be_return_nil(vm);
    }
    BerryEngine *eng = getEngine(vm);
    AppContext *ctx = eng ? eng->getAppContext() : nullptr;
    if (ctx) {
        uint8_t channel = (uint8_t)be_toint(vm, 1);
        const char *text = be_tostring(vm, 2);
        if (text)
            ctx->broadcastMessage(channel, text);
    } else {
        ILOG_ERROR("mesh.broadcast: no AppContext");
    }
    be_return_nil(vm);
}

// ---------------------------------------------------------------------------
// mesh.my_node() -> int
// ---------------------------------------------------------------------------
static int be_mesh_my_node(bvm *vm)
{
    BerryEngine *eng = getEngine(vm);
    AppContext *ctx = eng ? eng->getAppContext() : nullptr;
    uint32_t node = ctx ? ctx->getMyNodeNum() : 0;
    be_pushint(vm, (int)node);
    be_return(vm);
}

// ---------------------------------------------------------------------------
// store.set(key, value) -> bool
// ---------------------------------------------------------------------------
static int be_store_set(bvm *vm)
{
    int argc = be_top(vm);
    if (argc < 2) {
        be_pushbool(vm, false);
        be_return(vm);
    }
    BerryEngine *eng = getEngine(vm);
    AppContext *ctx = eng ? eng->getAppContext() : nullptr;
    bool ok = false;
    if (ctx) {
        const char *key = be_tostring(vm, 1);
        const char *val = be_tostring(vm, 2);
        if (key && val)
            ok = ctx->kvStore(key, val);
    } else {
        ILOG_ERROR("store.set: no AppContext");
    }
    be_pushbool(vm, ok);
    be_return(vm);
}

// ---------------------------------------------------------------------------
// store.get(key) -> string
// ---------------------------------------------------------------------------
static int be_store_get(bvm *vm)
{
    int argc = be_top(vm);
    if (argc < 1) {
        be_pushstring(vm, "");
        be_return(vm);
    }
    BerryEngine *eng = getEngine(vm);
    AppContext *ctx = eng ? eng->getAppContext() : nullptr;
    if (ctx) {
        const char *key = be_tostring(vm, 1);
        std::string val = key ? ctx->kvLoad(key) : "";
        be_pushstring(vm, val.c_str());
    } else {
        ILOG_ERROR("store.get: no AppContext");
        be_pushstring(vm, "");
    }
    be_return(vm);
}

#ifdef HAS_HTTP_CLIENT
// ---------------------------------------------------------------------------
// http.get(url) -> string
// ---------------------------------------------------------------------------
static int be_http_get(bvm *vm)
{
    int argc = be_top(vm);
    if (argc < 1) {
        be_pushstring(vm, "");
        be_return(vm);
    }
    const char *url = be_tostring(vm, 1);
    if (!url) {
        be_pushstring(vm, "");
        be_return(vm);
    }
    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000);
    int code = http.GET();
    if (code > 0) {
        std::string body = http.getString().c_str();
        http.end();
        be_pushstring(vm, body.c_str());
    } else {
        ILOG_ERROR("http.get: request failed, code=%d", code);
        http.end();
        be_pushstring(vm, "");
    }
    be_return(vm);
}

// ---------------------------------------------------------------------------
// http.post(url, body) -> string
// ---------------------------------------------------------------------------
static int be_http_post(bvm *vm)
{
    int argc = be_top(vm);
    if (argc < 2) {
        be_pushstring(vm, "");
        be_return(vm);
    }
    const char *url = be_tostring(vm, 1);
    const char *body = be_tostring(vm, 2);
    if (!url || !body) {
        be_pushstring(vm, "");
        be_return(vm);
    }
    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    if (code > 0) {
        std::string resp = http.getString().c_str();
        http.end();
        be_pushstring(vm, resp.c_str());
    } else {
        ILOG_ERROR("http.post: request failed, code=%d", code);
        http.end();
        be_pushstring(vm, "");
    }
    be_return(vm);
}
#endif // HAS_HTTP_CLIENT

// ---------------------------------------------------------------------------
// Register a module as a Berry map global: module.func = native_func
// ---------------------------------------------------------------------------
static void registerModule(bvm *vm, const char *modName,
                            const char *funcName, bntvfunc fn)
{
    // Get or create module map
    be_getglobal(vm, modName);
    if (!be_ismap(vm, -1)) {
        be_pop(vm, 1);
        be_newmap(vm);
        be_setglobal(vm, modName);
        be_getglobal(vm, modName);
    }
    // Push key and native closure, then map_insert
    be_pushstring(vm, funcName);
    be_pushntvfunction(vm, fn);
    be_data_insert(vm, -3);
    be_pop(vm, 2); // pop key and value
    be_pop(vm, 1); // pop map
}

void BerryEngine::registerBuiltins()
{
    if (!vm)
        return;

    // Store engine pointer as Berry global light userdata for native functions to retrieve
    be_pushcomptr(vm, (void *)this);
    be_setglobal(vm, "_eng");

    // mesh module
    registerModule(vm, "mesh", "send",      be_mesh_send);
    registerModule(vm, "mesh", "broadcast", be_mesh_broadcast);
    registerModule(vm, "mesh", "my_node",   be_mesh_my_node);

    // store module
    registerModule(vm, "store", "set", be_store_set);
    registerModule(vm, "store", "get", be_store_get);

#ifdef HAS_HTTP_CLIENT
    // http module
    registerModule(vm, "http", "get",  be_http_get);
    registerModule(vm, "http", "post", be_http_post);
#endif

    ILOG_INFO("BerryEngine: built-in modules registered (mesh, store%s)",
#ifdef HAS_HTTP_CLIENT
              ", http"
#else
              ""
#endif
    );
}

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
