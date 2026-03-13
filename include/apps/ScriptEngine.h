#pragma once

#ifdef HAS_CUSTOM_APPS

#include <functional>
#include <stddef.h>

/**
 * Abstract interface for embedded scripting engines.
 * Implementations: BerryEngine, ElkEngine
 */
class ScriptEngine
{
  public:
    using NativeFunc = std::function<void(void)>;

    virtual ~ScriptEngine() = default;

    /** Initialize the scripting VM with given heap size. */
    virtual bool init(size_t heapSize) = 0;

    /** Load and execute a script file from filesystem. */
    virtual bool loadScript(const char *path) = 0;

    /** Load and execute a script from string. */
    virtual bool loadString(const char *code) = 0;

    /** Call a script function by name (no arguments). Returns false if function not found. */
    virtual bool callFunction(const char *name) = 0;

    /** Call a script function with a string argument. */
    virtual bool callFunction(const char *name, const char *arg) = 0;

    /** Call a script function with two string arguments. */
    virtual bool callFunction(const char *name, const char *arg1, const char *arg2) = 0;

    /** Call a script function with an integer argument. */
    virtual bool callFunction(const char *name, int arg) = 0;

    /** Register a native C/C++ variable or function accessible from scripts. */
    virtual void registerNativeFunction(const char *name, NativeFunc fn) = 0;

    /** Get engine name (e.g. "Berry", "Elk"). */
    virtual const char *getEngineName() const = 0;

    /** Destroy the VM and free resources. */
    virtual void destroy() = 0;
};

#endif // HAS_CUSTOM_APPS
