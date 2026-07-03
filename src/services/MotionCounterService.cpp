#include "MotionCounterService.h"
#include <cstring>

namespace raven {
namespace {
// RTC_SLOW_MEM layout (must match Hublink-BEAM ULPManager.h): counters in 0–3, ULP program at 4+.
// Loading the program at index 1 collides with kInactivityCountAddr and corrupts inactivity_count.
enum : uint16_t {
  kMotionCountAddr = 0,
  kInactivityCountAddr = 1,
  kInactivityTrackerAddr = 2,
  kInactivityPeriodAddr = 3,
  kProgramStart = 4,
};

// ESP32-S3 ULP uses RTC_FAST (~17.5 MHz). Legacy ESP32 BEAM 40000/415 yields ~0.95 s windows
// on S3. Target ~1.0 s: iterations * (delay + ~12 insn cycles) / 17.5e6.
constexpr uint16_t kUlpSampleDelayCycles = 415;
constexpr uint16_t kUlpWindowIterations = 41100;

ulp_insn_t gUlpProgram[64];
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
  // motion. HIGH = motion (pulldown idle). Inactivity tracking matches legacy BEAM ULPManager.
  const ulp_insn_t programTemplate[] = {
      I_MOVI(R2, kMotionCountAddr),
      I_MOVI(R3, 1),

      M_LABEL(1),
      I_MOVI(R1, kUlpWindowIterations),

      M_LABEL(2),
      I_RD_REG(RTC_GPIO_IN_REG, rtcGpioIndex_ + RTC_GPIO_IN_NEXT_S,
               rtcGpioIndex_ + RTC_GPIO_IN_NEXT_S),
      M_BE(3, 0),
      I_MOVI(R3, 0),

      M_LABEL(3),
      I_DELAY(kUlpSampleDelayCycles),
      I_SUBI(R1, R1, 1),
      I_MOVR(R0, R1),
      M_BE(4, 0),
      I_MOVR(R0, R3),
      M_BE(2, 1),
      M_BX(3),

      M_LABEL(4),
      I_MOVR(R0, R3),
      M_BE(5, 0),
      M_BX(6),

      M_LABEL(5),
      I_LD(R1, R2, 0),
      I_ADDI(R1, R1, 1),
      I_ST(R1, R2, 0),
      M_BX(8),

      M_LABEL(6),
      I_MOVI(R1, kInactivityTrackerAddr),
      I_LD(R0, R1, 0),
      I_ADDI(R0, R0, 1),
      I_ST(R0, R1, 0),
      I_MOVI(R1, kInactivityPeriodAddr),
      I_LD(R1, R1, 0),
      I_SUBR(R0, R1, R0),
      M_BG(9, 1),

      I_MOVI(R1, kInactivityCountAddr),
      I_LD(R0, R1, 0),
      I_ADDI(R0, R0, 1),
      I_ST(R0, R1, 0),

      M_LABEL(8),
      I_MOVI(R0, 0),
      I_MOVI(R1, kInactivityTrackerAddr),
      I_ST(R0, R1, 0),

      M_LABEL(9),
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

void MotionCounterService::clearCount() {
  RTC_SLOW_MEM[kMotionCountAddr] = 0;
  clearInactivityCounters();
}

void MotionCounterService::setInactivityPeriod(uint16_t seconds) {
  RTC_SLOW_MEM[kInactivityPeriodAddr] = seconds;
}

uint16_t MotionCounterService::inactivityCount() const {
  return static_cast<uint16_t>(RTC_SLOW_MEM[kInactivityCountAddr] & 0xFFFF);
}

uint16_t MotionCounterService::inactivityTracker() const {
  return static_cast<uint16_t>(RTC_SLOW_MEM[kInactivityTrackerAddr] & 0xFFFF);
}

void MotionCounterService::clearInactivityCounters() {
  RTC_SLOW_MEM[kInactivityCountAddr] = 0;
  RTC_SLOW_MEM[kInactivityTrackerAddr] = 0;
}

} // namespace raven
