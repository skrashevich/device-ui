#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY
#ifdef HAS_AUDIO_PLAYER

/**
 * AudioPlayer bindings for the Berry scripting engine.
 *
 * Implements the audio.* module accessible from Berry scripts.
 * All functions delegate to AudioPlayer::instance() (singleton).
 *
 * Berry API:
 *   audio.play(path) -> bool
 *   audio.pause()
 *   audio.resume()
 *   audio.stop()
 *   audio.status() -> "playing" | "paused" | "stopped"
 *   audio.volume(level)   # 0-100
 *   audio.position() -> int   # ms
 *   audio.duration() -> int   # ms
 *   audio.file() -> string
 */

#include "apps/ScriptAudioBindings.h"
#include "apps/BerryEngine.h"
#include "apps/audio/AudioPlayer.h"
#include "berry.h"
#include "util/ILog.h"

// ---------------------------------------------------------------------------
// audio.play(path) -> bool
// ---------------------------------------------------------------------------
static int be_audio_play(bvm *vm)
{
    int argc = be_top(vm);
    if (argc < 1) {
        be_pushbool(vm, false);
        be_return(vm);
    }
    const char *path = be_tostring(vm, 1);
    if (!path) {
        be_pushbool(vm, false);
        be_return(vm);
    }
    bool ok = AudioPlayer::instance().play(path);
    be_pushbool(vm, ok);
    be_return(vm);
}

// ---------------------------------------------------------------------------
// audio.pause()
// ---------------------------------------------------------------------------
static int be_audio_pause(bvm *vm)
{
    AudioPlayer::instance().pause();
    be_return_nil(vm);
}

// ---------------------------------------------------------------------------
// audio.resume()
// ---------------------------------------------------------------------------
static int be_audio_resume(bvm *vm)
{
    AudioPlayer::instance().resume();
    be_return_nil(vm);
}

// ---------------------------------------------------------------------------
// audio.stop()
// ---------------------------------------------------------------------------
static int be_audio_stop(bvm *vm)
{
    AudioPlayer::instance().stop();
    be_return_nil(vm);
}

// ---------------------------------------------------------------------------
// audio.status() -> "playing" | "paused" | "stopped"
// ---------------------------------------------------------------------------
static int be_audio_status(bvm *vm)
{
    AudioPlayer::State state = AudioPlayer::instance().getState();
    const char *status = "stopped";
    if (state == AudioPlayer::State::PLAYING)
        status = "playing";
    else if (state == AudioPlayer::State::PAUSED)
        status = "paused";
    be_pushstring(vm, status);
    be_return(vm);
}

// ---------------------------------------------------------------------------
// audio.volume(level)  level: 0-100
// ---------------------------------------------------------------------------
static int be_audio_volume(bvm *vm)
{
    int argc = be_top(vm);
    if (argc < 1) {
        be_return_nil(vm);
    }
    int level = be_toint(vm, 1);
    if (level < 0)   level = 0;
    if (level > 100) level = 100;
    AudioPlayer::instance().setVolume((uint8_t)level);
    be_return_nil(vm);
}

// ---------------------------------------------------------------------------
// audio.position() -> int (ms)
// ---------------------------------------------------------------------------
static int be_audio_position(bvm *vm)
{
    uint32_t pos = AudioPlayer::instance().getPositionMs();
    be_pushint(vm, (int)pos);
    be_return(vm);
}

// ---------------------------------------------------------------------------
// audio.duration() -> int (ms)
// ---------------------------------------------------------------------------
static int be_audio_duration(bvm *vm)
{
    uint32_t dur = AudioPlayer::instance().getDurationMs();
    be_pushint(vm, (int)dur);
    be_return(vm);
}

// ---------------------------------------------------------------------------
// audio.file() -> string
// ---------------------------------------------------------------------------
static int be_audio_file(bvm *vm)
{
    const std::string &file = AudioPlayer::instance().getCurrentFile();
    be_pushstring(vm, file.c_str());
    be_return(vm);
}

// ---------------------------------------------------------------------------
// Module registration helper (same pattern as BerryEngine::registerBuiltins)
// ---------------------------------------------------------------------------
static void reg(bvm *vm, const char *mod, const char *fn, bntvfunc f)
{
    be_getglobal(vm, mod);
    if (!be_ismap(vm, -1)) {
        be_pop(vm, 1);
        be_newmap(vm);
        be_setglobal(vm, mod);
        be_getglobal(vm, mod);
    }
    be_pushstring(vm, fn);
    be_pushntvfunction(vm, f);
    be_data_insert(vm, -3);
    be_pop(vm, 2);
    be_pop(vm, 1);
}

// ---------------------------------------------------------------------------
// Public C interface
// ---------------------------------------------------------------------------

void ScriptAudioBindings_register(BerryEngine *engine)
{
    if (!engine)
        return;

    bvm *vm = engine->getVM();
    if (!vm)
        return;

    reg(vm, "audio", "play",     be_audio_play);
    reg(vm, "audio", "pause",    be_audio_pause);
    reg(vm, "audio", "resume",   be_audio_resume);
    reg(vm, "audio", "stop",     be_audio_stop);
    reg(vm, "audio", "status",   be_audio_status);
    reg(vm, "audio", "volume",   be_audio_volume);
    reg(vm, "audio", "position", be_audio_position);
    reg(vm, "audio", "duration", be_audio_duration);
    reg(vm, "audio", "file",     be_audio_file);

    ILOG_INFO("ScriptAudioBindings: audio module registered");
}

#endif // HAS_AUDIO_PLAYER
#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
