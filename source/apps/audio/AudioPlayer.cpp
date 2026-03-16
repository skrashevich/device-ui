#ifdef HAS_CUSTOM_APPS
#ifdef HAS_AUDIO_PLAYER

#include "apps/audio/AudioPlayer.h"
#include "util/ILog.h"

// ── libhelix-mp3 ────────────────────────────────────────────────────────────
#include "mp3dec.h"

// ── SD card backend ─────────────────────────────────────────────────────────
#if defined(ARCH_PORTDUINO)
#include "PortduinoFS.h"
#include <FS.h>
#define AUDIO_FS PortduinoFS
#define AUDIO_USE_ARDUINO_FS 1

#elif defined(HAS_SD_MMC)
#include "SD_MMC.h"
#include <FS.h>
#define AUDIO_FS SD_MMC
#define AUDIO_USE_ARDUINO_FS 1

#elif defined(HAS_SDCARD)
#include "SdFat.h"
extern SdFs SDFs;
#define AUDIO_USE_SDFAT 1

#else
#warning "AudioPlayer: no SD card backend detected — file I/O will not work"
#endif

// ── I2S / PWM output ────────────────────────────────────────────────────────
#ifndef HAS_AUDIO_PWM_FALLBACK
// Primary path: I2S with external DAC (MAX98357A, PCM5102, etc.)
#include "driver/i2s.h"

#ifndef AUDIO_I2S_PORT
#define AUDIO_I2S_PORT      I2S_NUM_0
#endif
#ifndef AUDIO_I2S_BCLK
#define AUDIO_I2S_BCLK      26
#endif
#ifndef AUDIO_I2S_LRCLK
#define AUDIO_I2S_LRCLK     25
#endif
#ifndef AUDIO_I2S_DOUT
#define AUDIO_I2S_DOUT      22
#endif
#ifndef AUDIO_SAMPLE_RATE
#define AUDIO_SAMPLE_RATE   44100
#endif

#else
// Fallback: ledc PWM output on a single GPIO (mono, 8-bit, lower quality)
#include "driver/ledc.h"
#ifndef AUDIO_PWM_GPIO
#define AUDIO_PWM_GPIO      42
#endif
#ifndef AUDIO_SAMPLE_RATE
#define AUDIO_SAMPLE_RATE   22050
#endif
#endif // HAS_AUDIO_PWM_FALLBACK

#include <algorithm>
#include <cstring>

// ── Helpers ─────────────────────────────────────────────────────────────────

#ifdef AUDIO_USE_ARDUINO_FS
// Wrap Arduino File in a heap allocation so we can store it as void*.
// We avoid including <FS.h> in the header to keep includes clean.
static fs::File *castFile(void *p) { return static_cast<fs::File *>(p); }
#endif

#ifdef AUDIO_USE_SDFAT
static FsFile *castFsFile(void *p) { return static_cast<FsFile *>(p); }
#endif

// ── AudioPlayer ─────────────────────────────────────────────────────────────

AudioPlayer &AudioPlayer::instance()
{
    static AudioPlayer inst;
    return inst;
}

AudioPlayer::AudioPlayer()
{
    memset(_readBuf, 0, sizeof(_readBuf));
    memset(_pcmBuf,  0, sizeof(_pcmBuf));
}

AudioPlayer::~AudioPlayer()
{
    stop();
    if (_decoder) {
        MP3FreeDecoder(static_cast<HMP3Decoder>(_decoder));
        _decoder = nullptr;
    }
#ifndef HAS_AUDIO_PWM_FALLBACK
    if (_i2sReady) {
        i2s_driver_uninstall(static_cast<i2s_port_t>(AUDIO_I2S_PORT));
        _i2sReady = false;
    }
#endif
}

// ── begin() ─────────────────────────────────────────────────────────────────

bool AudioPlayer::begin()
{
    // Initialise MP3 decoder
    if (!_decoder) {
        _decoder = MP3InitDecoder();
        if (!_decoder) {
            ILOG_ERROR("AudioPlayer: MP3InitDecoder failed");
            return false;
        }
    }

    if (!_i2sReady) {
        if (!initI2S()) {
            return false;
        }
    }

    ILOG_INFO("AudioPlayer: ready");
    return true;
}

// ── initI2S() ───────────────────────────────────────────────────────────────

bool AudioPlayer::initI2S()
{
#ifndef HAS_AUDIO_PWM_FALLBACK
    i2s_config_t cfg = {};
    cfg.mode                 = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = AUDIO_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 64;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;

    esp_err_t err = i2s_driver_install(static_cast<i2s_port_t>(AUDIO_I2S_PORT), &cfg, 0, nullptr);
    if (err != ESP_OK) {
        ILOG_ERROR("AudioPlayer: i2s_driver_install failed: %d", err);
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = AUDIO_I2S_BCLK;
    pins.ws_io_num    = AUDIO_I2S_LRCLK;
    pins.data_out_num = AUDIO_I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    err = i2s_set_pin(static_cast<i2s_port_t>(AUDIO_I2S_PORT), &pins);
    if (err != ESP_OK) {
        ILOG_ERROR("AudioPlayer: i2s_set_pin failed: %d", err);
        i2s_driver_uninstall(static_cast<i2s_port_t>(AUDIO_I2S_PORT));
        return false;
    }

    i2s_zero_dma_buffer(static_cast<i2s_port_t>(AUDIO_I2S_PORT));
    _i2sReady = true;
    ILOG_DEBUG("AudioPlayer: I2S initialised (port %d, rate %d)", AUDIO_I2S_PORT, AUDIO_SAMPLE_RATE);
    return true;

#else
    // PWM fallback — configure LEDC for audio output on AUDIO_PWM_GPIO
    ledc_timer_config_t timer = {};
    timer.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer.timer_num       = LEDC_TIMER_0;
    timer.duty_resolution = LEDC_TIMER_8_BIT;
    timer.freq_hz         = AUDIO_SAMPLE_RATE;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer) != ESP_OK) {
        ILOG_ERROR("AudioPlayer: ledc_timer_config failed");
        return false;
    }

    ledc_channel_config_t ch = {};
    ch.gpio_num   = AUDIO_PWM_GPIO;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel    = LEDC_CHANNEL_0;
    ch.timer_sel  = LEDC_TIMER_0;
    ch.duty       = 128; // 50 % idle
    ch.hpoint     = 0;
    if (ledc_channel_config(&ch) != ESP_OK) {
        ILOG_ERROR("AudioPlayer: ledc_channel_config failed");
        return false;
    }

    _i2sReady = true; // reuse flag
    ILOG_DEBUG("AudioPlayer: PWM fallback on GPIO %d", AUDIO_PWM_GPIO);
    return true;
#endif
}

// ── play() ──────────────────────────────────────────────────────────────────

bool AudioPlayer::play(const char *path)
{
    if (!_decoder || !_i2sReady) {
        ILOG_ERROR("AudioPlayer: call begin() first");
        return false;
    }

    stop(); // close any previous file

#ifdef AUDIO_USE_ARDUINO_FS
    auto *f = new fs::File();
    *f = AUDIO_FS.open(path, FILE_READ);
    if (!*f) {
        ILOG_ERROR("AudioPlayer: cannot open %s", path);
        delete f;
        return false;
    }
    _fileSizeBytes = (uint32_t)f->size();
    _file          = f;
    _fileIsArduino = true;

#elif defined(AUDIO_USE_SDFAT)
    auto *f = new FsFile();
    if (!f->open(path, O_RDONLY)) {
        ILOG_ERROR("AudioPlayer: cannot open %s", path);
        delete f;
        return false;
    }
    _fileSizeBytes = (uint32_t)f->size();
    _file          = f;
    _fileIsArduino = false;

#else
    ILOG_ERROR("AudioPlayer: no SD backend — cannot open %s", path);
    return false;
#endif

    _currentFile   = path;
    _framesDecoded = 0;
    _readBufFill   = 0;
    _readBufOfs    = 0;
    _state         = State::PLAYING;

    ILOG_INFO("AudioPlayer: playing %s (%lu bytes)", path, (unsigned long)_fileSizeBytes);
    return true;
}

// ── pause / resume / stop ───────────────────────────────────────────────────

void AudioPlayer::pause()
{
    if (_state == State::PLAYING) {
        _state = State::PAUSED;
#ifndef HAS_AUDIO_PWM_FALLBACK
        i2s_zero_dma_buffer(static_cast<i2s_port_t>(AUDIO_I2S_PORT));
#endif
        ILOG_DEBUG("AudioPlayer: paused");
    }
}

void AudioPlayer::resume()
{
    if (_state == State::PAUSED) {
        _state = State::PLAYING;
        ILOG_DEBUG("AudioPlayer: resumed");
    }
}

void AudioPlayer::stop()
{
    if (_state != State::STOPPED) {
        _state = State::STOPPED;
#ifndef HAS_AUDIO_PWM_FALLBACK
        i2s_zero_dma_buffer(static_cast<i2s_port_t>(AUDIO_I2S_PORT));
#endif
    }
    closeFile();
    _framesDecoded = 0;
    _readBufFill   = 0;
    _readBufOfs    = 0;
    _currentFile.clear();
    ILOG_DEBUG("AudioPlayer: stopped");
}

void AudioPlayer::closeFile()
{
    if (!_file)
        return;

#ifdef AUDIO_USE_ARDUINO_FS
    if (_fileIsArduino) {
        castFile(_file)->close();
        delete castFile(_file);
    }
#endif
#ifdef AUDIO_USE_SDFAT
    if (!_fileIsArduino) {
        castFsFile(_file)->close();
        delete castFsFile(_file);
    }
#endif
    _file = nullptr;
}

// ── setVolume() ──────────────────────────────────────────────────────────────

void AudioPlayer::setVolume(uint8_t volume)
{
    _volume = (volume > 100) ? 100 : volume;
}

// ── applyVolume() ────────────────────────────────────────────────────────────

int16_t AudioPlayer::applyVolume(int16_t sample) const
{
    if (_volume == 100)
        return sample;
    if (_volume == 0)
        return 0;
    return static_cast<int16_t>((static_cast<int32_t>(sample) * _volume) / 100);
}

// ── getPositionMs() / getDurationMs() ────────────────────────────────────────

uint32_t AudioPlayer::getPositionMs() const
{
    // Each MP3 frame is 1152 PCM samples at the stream sample rate.
    // Use _avgBitrate as a proxy for the sample rate until we parse
    // the frame header — good enough for progress display.
    if (_framesDecoded == 0)
        return 0;
    // 1152 samples per frame × 1000 ms/s ÷ sample_rate
    return (_framesDecoded * 1152UL * 1000UL) / AUDIO_SAMPLE_RATE;
}

uint32_t AudioPlayer::getDurationMs() const
{
    if (_fileSizeBytes == 0 || _avgBitrate == 0)
        return 0;
    // duration = fileSize × 8 ÷ bitrate  (in ms)
    return (uint32_t)(((uint64_t)_fileSizeBytes * 8ULL * 1000ULL) / _avgBitrate);
}

// ── tick() ───────────────────────────────────────────────────────────────────

void AudioPlayer::tick()
{
    if (_state != State::PLAYING || !_file)
        return;

    // ── 1. Refill read buffer ────────────────────────────────────────────────
    // Shift unconsumed bytes to the front
    int remaining = _readBufFill - _readBufOfs;
    if (remaining < 0)
        remaining = 0;

    if (remaining > 0 && _readBufOfs > 0) {
        memmove(_readBuf, _readBuf + _readBufOfs, (size_t)remaining);
    }
    _readBufOfs  = 0;
    _readBufFill = remaining;

    // Top up the buffer from SD
    int toRead = (int)READ_BUF_SIZE - remaining;
    if (toRead > 0) {
        int got = 0;
#ifdef AUDIO_USE_ARDUINO_FS
        if (_fileIsArduino) {
            fs::File *f = castFile(_file);
            if (f->available()) {
                got = (int)f->read(_readBuf + remaining, (size_t)toRead);
            }
        }
#endif
#ifdef AUDIO_USE_SDFAT
        if (!_fileIsArduino) {
            FsFile *f = castFsFile(_file);
            got = (int)f->read(_readBuf + remaining, (size_t)toRead);
        }
#endif
        if (got > 0)
            _readBufFill += got;
    }

    if (_readBufFill == 0) {
        // EOF
        ILOG_INFO("AudioPlayer: end of file — stopping");
        stop();
        return;
    }

    // ── 2. Decode one MP3 frame ──────────────────────────────────────────────
    uint8_t *inPtr  = _readBuf + _readBufOfs;
    int      inLeft = _readBufFill - _readBufOfs;

    int err = MP3Decode(static_cast<HMP3Decoder>(_decoder),
                        &inPtr, &inLeft,
                        _pcmBuf, 0 /* mono output flag: 0 = stereo */);

    // Advance the offset by however many bytes the decoder consumed
    int consumed = (_readBufFill - _readBufOfs) - inLeft;
    _readBufOfs += consumed;

    if (err != ERR_MP3_NONE) {
        if (err == ERR_MP3_INDATA_UNDERFLOW) {
            // Need more data — will refill on next tick
            return;
        }
        if (err == ERR_MP3_MAINDATA_UNDERFLOW) {
            // Bit-reservoir underflow at stream start — not fatal
            return;
        }
        ILOG_WARN("AudioPlayer: MP3Decode error %d — skipping", err);
        // Advance past one byte to re-sync
        if (_readBufOfs < _readBufFill)
            _readBufOfs++;
        return;
    }

    // ── 3. Read frame info (sample rate, bitrate, channels, outputSamps) ────
    MP3FrameInfo info;
    MP3GetLastFrameInfo(static_cast<HMP3Decoder>(_decoder), &info);

    _framesDecoded++;
    // Update rolling average bitrate (for getDurationMs)
    if (info.bitrate > 0) {
        _avgBitrate = (_avgBitrate * 7 + (uint32_t)info.bitrate) / 8;
    }

    // Update I2S sample rate if it differs from what we configured
#ifndef HAS_AUDIO_PWM_FALLBACK
    if (info.samprate > 0 && (uint32_t)info.samprate != AUDIO_SAMPLE_RATE) {
        i2s_set_clk(static_cast<i2s_port_t>(AUDIO_I2S_PORT),
                    (uint32_t)info.samprate,
                    I2S_BITS_PER_SAMPLE_16BIT,
                    info.nChans == 1 ? I2S_CHANNEL_MONO : I2S_CHANNEL_STEREO);
    }
#endif

    // ── 4. Apply volume ──────────────────────────────────────────────────────
    int totalSamples = info.outputSamps; // nChans × samples-per-channel
    for (int i = 0; i < totalSamples; i++) {
        _pcmBuf[i] = applyVolume(_pcmBuf[i]);
    }

    // ── 5. Write to output ───────────────────────────────────────────────────
#ifndef HAS_AUDIO_PWM_FALLBACK
    size_t bytesToWrite = (size_t)totalSamples * sizeof(int16_t);
    size_t written      = 0;
    i2s_write(static_cast<i2s_port_t>(AUDIO_I2S_PORT),
              _pcmBuf, bytesToWrite, &written,
              portMAX_DELAY);
    if (written < bytesToWrite) {
        ILOG_WARN("AudioPlayer: I2S write incomplete (%u/%u bytes)",
                  (unsigned)written, (unsigned)bytesToWrite);
    }

#else
    // PWM fallback: output mono, 8-bit via LEDC duty cycle
    // Mix stereo to mono and scale to 0-255
    int step = (info.nChans == 2) ? 2 : 1;
    for (int i = 0; i < totalSamples; i += step) {
        int32_t mono = _pcmBuf[i];
        if (info.nChans == 2)
            mono = (mono + _pcmBuf[i + 1]) / 2;
        // int16 → uint8: shift sign bit, then scale to 8 bits
        uint32_t duty = (uint32_t)((mono + 32768) >> 8);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        // Busy-wait one sample period (very rough, no timer used)
        // In production this should be driven by a hardware timer ISR.
        uint32_t cycles = (CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ * 1000000UL) / AUDIO_SAMPLE_RATE;
        volatile uint32_t c = cycles;
        while (c--)
            __asm__ __volatile__("nop");
    }
#endif
}

#endif // HAS_AUDIO_PLAYER
#endif // HAS_CUSTOM_APPS
