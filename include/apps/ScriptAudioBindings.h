#pragma once

#ifdef HAS_CUSTOM_APPS
#ifdef HAS_SCRIPTING_BERRY
#ifdef HAS_AUDIO_PLAYER

class BerryEngine;

/**
 * Register the audio.* Berry module with the given engine.
 * Must be called after BerryEngine::init() and before loading any scripts.
 *
 * Wraps AudioPlayer singleton for use from Berry scripts:
 *   audio.play(path) -> bool       — open file and start playback
 *   audio.pause()                  — pause playback
 *   audio.resume()                 — resume paused playback
 *   audio.stop()                   — stop and close file
 *   audio.status() -> string       — "playing" | "paused" | "stopped"
 *   audio.volume(level)            — set volume 0-100
 *   audio.position() -> int        — playback position in ms
 *   audio.duration() -> int        — total duration in ms (0 if unknown)
 *   audio.file() -> string         — path of currently open file
 */
void ScriptAudioBindings_register(BerryEngine *engine);

#endif // HAS_AUDIO_PLAYER
#endif // HAS_SCRIPTING_BERRY
#endif // HAS_CUSTOM_APPS
