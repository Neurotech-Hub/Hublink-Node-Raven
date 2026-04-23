#include "MagnetCounterService.h"

namespace raven {
namespace {
enum : uint16_t {
  kEdgeCountAddr = 0,
  kProgramStart = 1,
};

ulp_insn_t gUlpProgram[20];
} // namespace

bool MagnetCounterService::begin(gpio_num_t sensorPin) {
  sensorPin_ = sensorPin;
  rtcGpioIndex_ = rtc_io_number_get(sensorPin_);

  rtc_gpio_init(sensorPin_);
  rtc_gpio_set_direction(sensorPin_, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(sensorPin_);
  rtc_gpio_pulldown_dis(sensorPin_);
  rtc_gpio_hold_en(sensorPin_);

  initialized_ = true;
  return true;
}

bool MagnetCounterService::start() {
  if (!initialized_ && !begin(sensorPin_)) {
    return false;
  }

  const ulp_insn_t programTemplate[] = {
      I_MOVI(R3, 0),
      I_RD_REG(RTC_GPIO_IN_REG, rtcGpioIndex_ + RTC_GPIO_IN_NEXT_S,
               rtcGpioIndex_ + RTC_GPIO_IN_NEXT_S),
      I_MOVR(R2, R0),

      M_LABEL(1),
      I_RD_REG(RTC_GPIO_IN_REG, rtcGpioIndex_ + RTC_GPIO_IN_NEXT_S,
               rtcGpioIndex_ + RTC_GPIO_IN_NEXT_S),
      I_MOVR(R1, R0),
      I_SUBR(R0, R1, R2),
      I_BL(5, 1),
      I_ADDI(R3, R3, 1),
      I_MOVR(R2, R1),

      I_MOVI(R1, kEdgeCountAddr),
      I_ST(R3, R1, 0),

      I_DELAY(0xFFFF),
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

uint16_t MagnetCounterService::edgeCount() const {
  return static_cast<uint16_t>(RTC_SLOW_MEM[kEdgeCountAddr] & 0xFFFF);
}

uint16_t MagnetCounterService::magnetPassCount() const {
  return static_cast<uint16_t>(edgeCount() / 2);
}

void MagnetCounterService::clearCount() { RTC_SLOW_MEM[kEdgeCountAddr] = 0; }

} // namespace raven
