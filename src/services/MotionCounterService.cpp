#include "MotionCounterService.h"
#include <cstring>

namespace raven {
namespace {
enum : uint16_t {
  kMotionCountAddr = 0,
  kProgramStart = 1,
};

ulp_insn_t gUlpProgram[40];
} // namespace

bool MotionCounterService::begin(gpio_num_t sensorPin) {
  sensorPin_ = sensorPin;
  rtcGpioIndex_ = rtc_io_number_get(sensorPin_);

  rtc_gpio_init(sensorPin_);
  rtc_gpio_set_direction(sensorPin_, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_dis(sensorPin_);
  rtc_gpio_pulldown_en(sensorPin_);
  rtc_gpio_hold_en(sensorPin_);

  initialized_ = true;
  return true;
}

bool MotionCounterService::start() {
  if (!initialized_ && !begin(sensorPin_)) {
    return false;
  }

  // Port of BEAM ULPManager PIR program: 1-second sampling windows, +1 count per window with
  // motion. HIGH = motion (pulldown idle). Inactivity tracking omitted in v1.
  const ulp_insn_t programTemplate[] = {
      I_MOVI(R2, kMotionCountAddr),
      I_MOVI(R3, 1),

      M_LABEL(1),
      I_MOVI(R1, 40000),

      M_LABEL(2),
      I_RD_REG(RTC_GPIO_IN_REG, rtcGpioIndex_ + RTC_GPIO_IN_NEXT_S,
               rtcGpioIndex_ + RTC_GPIO_IN_NEXT_S),
      M_BE(3, 0),
      I_MOVI(R3, 0),

      M_LABEL(3),
      I_DELAY(415),
      I_SUBI(R1, R1, 1),
      I_MOVR(R0, R1),
      M_BE(4, 0),
      I_MOVR(R0, R3),
      M_BE(2, 1),
      M_BX(3),

      M_LABEL(4),
      I_MOVR(R0, R3),
      M_BE(5, 0),
      M_BX(8),

      M_LABEL(5),
      I_LD(R1, R2, 0),
      I_ADDI(R1, R1, 1),
      I_ST(R1, R2, 0),

      M_LABEL(8),
      I_MOVI(R3, 1),
      M_BX(1),
  };

  memcpy(gUlpProgram, programTemplate, sizeof(programTemplate));
  size_t size = sizeof(programTemplate) / sizeof(ulp_insn_t);
  esp_err_t err = ulp_process_macros_and_load(kProgramStart, gUlpProgram, &size);
  if (err != ESP_OK) {
    return false;
  }

  err = ulp_run(kProgramStart);
  return err == ESP_OK;
}

uint16_t MotionCounterService::motionCount() const {
  return static_cast<uint16_t>(RTC_SLOW_MEM[kMotionCountAddr] & 0xFFFF);
}

void MotionCounterService::clearCount() { RTC_SLOW_MEM[kMotionCountAddr] = 0; }

} // namespace raven
