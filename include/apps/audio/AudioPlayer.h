#pragma once

#ifdef HAS_CUSTOM_APPS
#ifdef HAS_AUDIO_PLAYER

#include <stdint.h>
#include <string>

/**
 * @brief Singleton MP3 audio player for ESP32-S3 (T-Deck).
 *
 * Uses libhelix-mp3 for decoding and ESP-IDF I2S driver for output.
 * ESP32-S3 has no built-in DAC — I2S output targets an external DAC
 * (e.g. MAX98357A or PCM5102). Falls back to PWM on GPIO 42 if
 * HAS_AUDIO_PWM_FALLBACK is defined.
 *
 * Typical usage:
 *   AudioPlayer::instance().begin();
 *   AudioPlayer::instance().play("/sdcard/song.mp3");
 *   // call tick() from main loop / task
 *   AudioPlayer::instance().tick();
 */
class AudioPlayer
{
  public:
    enum class State {
        STOPPED,
        PLAYING,
        PAUSED,
    };

    /// Returns the singleton instance.
    static AudioPlayer &instance();

    // Non-copyable, non-movable
    AudioPlayer(const AudioPlayer &) = delete;
    AudioPlayer &operator=(const AudioPlayer &) = delete;

    /**
     * @brief Initialise I2S peripheral and MP3 decoder.
     *        Must be called once before any other method.
     * @return true on success.
     */
    bool begin();

    /**
     * @brief Open and start playing a file from SD card.
     * @param path  Absolute path on the SD filesystem, e.g. "/music/song.mp3"
     * @return true if file was opened successfully and playback started.
     */
    bool play(const char *path);

    /// Pause playback (retains file position).
    void pause();

    /// Resume a paused stream.
    void resume();

    /// Stop playback and close the file.
    void stop();

    /**
     * @brief Set output volume.
     * @param volume  0 (mute) … 100 (maximum).
     */
    void setVolume(uint8_t volume);

    /// @return Current playback state.
    State getState() const { return _state; }

    /// @return Path of the currently open file, or empty string.
    const std::string &getCurrentFile() const { return _currentFile; }

    /**
     * @brief Decode and output one MP3 frame.
     *        Call from the main loop or a dedicated audio task at ~40 ms intervals.
     */
    void tick();

    /**
     * @brief Estimated playback position in milliseconds.
     *        Derived from decoded frame count × average frame duration.
     */
    uint32_t getPositionMs() const;

    /**
     * @brief Estimated total duration in milliseconds.
     *        Available after the first frame has been decoded (uses bitrate info).
     *        Returns 0 if unknown.
     */
    uint32_t getDurationMs() const;

  private:
    AudioPlayer();
    ~AudioPlayer();

    bool initI2S();
    void closeFile();

    /// Scale a 16-bit PCM sample by _volume (0-100).
    int16_t applyVolume(int16_t sample) const;

    State       _state       = State::STOPPED;
    std::string _currentFile;

    // Volume 0-100
    uint8_t _volume = 80;

    // MP3 decoder handle (HMP3Decoder from libhelix-mp3)
    void *_decoder = nullptr;

    // I2S initialised flag
    bool _i2sReady = false;

    // File handle — type depends on available SD backend
#if defined(HAS_SD_MMC) || defined(ARCH_PORTDUINO)
    // Arduino-style File (SD_MMC / PortduinoFS)
    void *_file = nullptr;   // stored as void* to avoid header pulling FS.h here
    bool  _fileIsArduino = true;
#elif defined(HAS_SDCARD)
    // SdFat FsFile
    void *_file = nullptr;
    bool  _fileIsArduino = false;
#else
    void *_file = nullptr;
    bool  _fileIsArduino = false;
#endif

    // Decode statistics
    uint32_t _framesDecoded = 0;
    uint32_t _avgBitrate    = 128000; // initial assumption, updated per frame
    uint32_t _fileSizeBytes = 0;

    // Intermediate read buffer fed to the MP3 decoder
    // libhelix requires up to MAINBUF_SIZE (1940) bytes look-ahead
    static constexpr size_t READ_BUF_SIZE = 4096;
    uint8_t  _readBuf[READ_BUF_SIZE];
    int      _readBufFill = 0;   // bytes currently in _readBuf
    int      _readBufOfs  = 0;   // current read position in _readBuf

    // PCM output buffer: one MP3 frame = up to 1152 samples * 2 ch * 2 bytes
    static constexpr size_t PCM_BUF_SAMPLES = 1152 * 2;
    int16_t  _pcmBuf[PCM_BUF_SAMPLES];
};

#endif // HAS_AUDIO_PLAYER
#endif // HAS_CUSTOM_APPS
