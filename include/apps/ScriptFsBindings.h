#pragma once

#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY

// Forward declare Berry VM type
struct bvm;

/**
 * Register the fs.* Berry module with the given VM.
 * Must be called after BerryEngine::init() and before loading any scripts.
 *
 * Berry API:
 *   fs.list_dir(path [, ext]) -> list   filenames in directory; optional extension filter (e.g. ".mp3")
 *   fs.file_size(path)        -> int    file size in bytes; -1 on error
 *   fs.exists(path)           -> bool   true if file/directory exists
 *
 * Security: paths containing ".." are rejected.
 */
void ScriptFSBindings_register(bvm *vm);

#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
