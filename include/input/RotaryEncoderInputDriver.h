#pragma once

#include "input/InputDriver.h"
#include <functional>

class RotaryEncoder;

/**
 * @brief Input driver for quadrature rotary encoders with push button.
 *        Uses the RotaryEncoder library for proper quadrature decoding.
 *        Designed for devices like T-Lora-Pager that have a scroll wheel.
 */
class RotaryEncoderInputDriver : public InputDriver
{
  public:
    using BootButtonCallback = std::function<void()>;

    RotaryEncoderInputDriver(void);
    virtual void init(void) override;
    virtual void task_handler(void) override;
    virtual ~RotaryEncoderInputDriver(void);

    // Optional auxiliary BOOT button (e.g. ESP32-S3 GPIO0 on T-LoRa-Pager).
    // The callback is invoked once per press (debounced, edge-triggered).
    static void setBootButtonCallback(BootButtonCallback cb) { bootButtonCallback = cb; }

  protected:
    static void encoder_read(lv_indev_t *indev, lv_indev_data_t *data);

  private:
    static RotaryEncoder *rotary;
    static volatile int16_t encoderDiff;
    static uint32_t lastStepTime;
    static BootButtonCallback bootButtonCallback;
    static bool bootButtonPressed;
    static uint32_t bootButtonLastChangeTime;
};
