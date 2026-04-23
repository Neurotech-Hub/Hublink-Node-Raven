/*
 * AlertPinTest — exercise PIN_ALERT (active-low) from DS3231 and optional MAX17048
 *
 * Hardware: ~RTC_INT and ~FUEL_ALERT into an AND gate, output to GPIO PIN_ALERT.
 * When either source pulls its line LOW, the combined alert line goes LOW.
 *
 * Notes:
 * - I2C rail must be on (Raven: PIN_I2C_EN) or DS3231 is unreachable. Keep
 *   I2C_EN asserted for the RTC portion. If the RTC is not powered, skip or fix power.
 * - MAX17048: Adafruit `cellVoltage()` returns `NaN` if `isDeviceReady()` is false.
 *   Never display that as 0.000 V — a past bug did that and hid real state. With a
 *   present pack, a true ~0.000 V read usually means a VCELL sense or bring-up
 *   issue; we print chip version/ID to help. After `wake()`, allow ~200ms before read.
 * - RTClib::setAlarm1() requires INTCN=1 in DS3231 control 0x0E bit 2; we set that
 *   before setAlarm1().
 */

#include <Adafruit_MAX1704X.h>
#include <HublinkNodeRaven.h>
#include <RTClib.h>
#include <esp_log.h>
#include <Wire.h>

// DS3231 (same as RTClib; local helpers for a tiny control tweak)
static constexpr uint8_t kDs3231Addr = 0x68;
static constexpr uint8_t kDs3231Control = 0x0E;
static constexpr uint8_t kDs3231Status = 0x0F;

static bool ds3231SetIntcn() {
  Wire.beginTransmission(kDs3231Addr);
  Wire.write(kDs3231Control);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(kDs3231Addr), 1) != 1) {
    return false;
  }
  uint8_t c = Wire.read();
  c = (c & 0xE3) | 0x04; // INTCN=1, RS0/RS1=0
  Wire.beginTransmission(kDs3231Addr);
  Wire.write(kDs3231Control);
  Wire.write(c);
  return Wire.endTransmission() == 0;
}

static void ds3231ClearIntFlags() {
  Wire.beginTransmission(kDs3231Addr);
  Wire.write(kDs3231Status);
  if (Wire.endTransmission() != 0) {
    return;
  }
  if (Wire.requestFrom(static_cast<int>(kDs3231Addr), 1) != 1) {
    return;
  }
  uint8_t s = Wire.read();
  s = static_cast<uint8_t>(s & static_cast<uint8_t>(~0x03U)); // A1F, A2F
  Wire.beginTransmission(kDs3231Addr);
  Wire.write(kDs3231Status);
  Wire.write(s);
  Wire.endTransmission();
}

raven::HublinkNode node;
RTC_DS3231 rtc;
Adafruit_MAX17048 fuel;

/** Print VCELL; does not treat NaN as 0.00 (that was misleading in early revision). */
static void printMax17048Status(const __FlashStringHelper *label) {
  Serial.print(label);
  Serial.print(F("  ready="));
  Serial.print(fuel.isDeviceReady() ? F("y") : F("n"));
  Serial.print(F("  chip=0x"));
  Serial.print(fuel.getChipID(), HEX);
  Serial.print(F("  icVer=0x"));
  Serial.print(fuel.getICversion(), HEX);
  Serial.print(F("  VCELL="));
  const float v = fuel.cellVoltage();
  if (isnan(v)) {
    Serial.print(F("NaN (not valid per driver — not the same as 0.000V)"));
  } else {
    Serial.print(v, 3);
    Serial.print(F(" V"));
    if (v < 0.01f) {
      Serial.print(
          F("  [if pack is present, check VBAT/VCELL sense to MAX17048; may need "
            "settling time]"));
    }
  }
  Serial.println();
}

bool gMaxOk = false;
// Independent interrupt-path checks (set true only when the specific IC showed the
// expected condition, so COMBINED success means both /INT sources were exercised).
bool gDs3231IntOk = false;
bool gMax17048IntOk = false;
bool gFuelTestSkipped = false;
static uint8_t gFuelAmbiguousPrints = 0;
enum class Phase : uint8_t {
  WaitRtc,
  RtcCleared,
  ArmFuel,
  WaitFuel,
  Done,
};

Phase gPhase = Phase::WaitRtc;
uint32_t gPhaseTime = 0;
bool gPrevLow = false;

static void printFinalResult() {
  Serial.println();
  Serial.println(F("========== ALERT TEST RESULT =========="));
  Serial.print(F("DS3231  (~RTC /INT, via A1F):  "));
  Serial.println(gDs3231IntOk ? F("OK") : F("NOT CONFIRMED"));
  if (!gMaxOk) {
    Serial.println(F("MAX17048:                     SKIPPED (not ready at boot)"));
  } else if (gFuelTestSkipped) {
    Serial.print(F("MAX17048  (~ALRT):            "));
    Serial.println(F("SKIPPED (VCELL invalid/low)"));
  } else {
    Serial.print(F("MAX17048  (~ALRT, VHi flag):  "));
    Serial.println(gMax17048IntOk ? F("OK") : F("NOT CONFIRMED"));
  }
  Serial.print(F("COMBINED:                      "));
  if (gDs3231IntOk && gMax17048IntOk) {
    Serial.println(F("SUCCESS — both sources independently asserted PIN_ALERT, then only "
                     "the fuel after RTC was cleared with expected flags."));
  } else if (gDs3231IntOk && (gFuelTestSkipped || !gMaxOk)) {
    Serial.println(
        F("PARTIAL — DS3231 OK; MAX17048 fuel alert not tested. Re-run with valid pack "
          "and VCELL for full test."));
  } else if (gMax17048IntOk && !gDs3231IntOk) {
    Serial.println(F("PARTIAL — MAX17048 OK; DS3231 not confirmed (unexpected)."));
  } else {
    Serial.println(F("FAIL or INCOMPLETE — see messages above."));
  }
  Serial.println(F("======================================"));
}

// Optional: set true to also wait for a MAX17048 window after the RTC part.
constexpr bool kRunFuelTest = true;
// Time until the DS3231 alarm (minute + second match, single shot within the hour)
constexpr int kRtcFirstFireSeconds = 12;
// Wait for RTC: alarm time + margin (ms)
static constexpr uint32_t kWaitRtcTimeoutMs = 40000;

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) {
    delay(10);
  }

  esp_log_level_set("i2c.master", ESP_LOG_NONE);
  node.beginHardware();
  node.setI2CPowerEnabled(true);
  node.beginI2C();

  pinMode(raven::PIN_ALERT, INPUT);
  pinMode(raven::PIN_LED_GREEN, OUTPUT);
  digitalWrite(raven::PIN_LED_GREEN, LOW);

  if (!rtc.begin(&Wire)) {
    Serial.println(F("DS3231: not found. Check I2C_EN, wiring, and power."));
  } else {
    if (!ds3231SetIntcn()) {
      Serial.println(F("DS3231: could not set INTCN (alarm on /INT)"));
    }
    ds3231ClearIntFlags();

    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    // Alarm1: full minute+second match so it fires once, ~kRtcFirstFireSeconds
    // from `now` (not once per second / once per minute repeat modes)
    const DateTime now = rtc.now();
    const DateTime alarm = now + TimeSpan(0, 0, 0, kRtcFirstFireSeconds);
    if (rtc.setAlarm1(alarm, DS3231_A1_Minute)) {
      Serial.print(F("DS3231: alarm1 armed for "));
      Serial.print(alarm.timestamp(DateTime::TIMESTAMP_TIME));
      Serial.print(F(" (minute+second, ~"));
      Serial.print(kRtcFirstFireSeconds);
      Serial.println(F(" s)"));
    } else {
      Serial.println(F("DS3231: setAlarm1 failed (INTCN? valid time?)"));
    }
  }

  gMaxOk = fuel.begin(&Wire) && fuel.isDeviceReady();
  if (!gMaxOk) {
    Serial.println(
        F("MAX17048: not ready (unplugged / weak pack is common)."));
  } else {
    fuel.wake();
    delay(200); // like bring-up: allow conversion after exit hibernate
    printMax17048Status(F("MAX17048 (startup):"));
  }

  gPhase = Phase::WaitRtc;
  gPhaseTime = millis();
  gPrevLow = digitalRead(raven::PIN_ALERT) == LOW;
  Serial.println(F("Watch GPIO PIN_ALERT (active LOW) and the green LED (on when LOW)."));
  Serial.println(F("---"));
}

void tryArmFuel() {
  if (!gMaxOk || !kRunFuelTest) {
    gFuelTestSkipped = true;
    gPhase = Phase::Done;
    return;
  }
  fuel.wake();
  delay(200);
  float v = fuel.cellVoltage();
  for (uint8_t n = 0; n < 5 && (isnan(v) || v < 0.1f); n++) {
    delay(100);
    v = fuel.cellVoltage();
  }
  printMax17048Status(F("MAX17048 (arm fuel test):"));
  if (isnan(v) || v < 0.25f) {
    Serial.println(
        F("MAX17048: skip fuel ALRT test — NaN or V < 0.25V (if battery is on-board, "
          "see note above)"));
    gFuelTestSkipped = true;
    gPhase = Phase::Done;
    fuel.hibernate();
    return;
  }
  // "Absurd" window: any real cell is above 0.5 V → V Hi alert, without needing a
  // "below min" (which would also trip on a 3.7 V pack if min were set to 4.2 V
  // together with a hi flag). Min 0 = no undervoltage trip for normal use.
  const float minV = 0.0f;
  const float maxV = 0.5f; // must exceed 0.5 V to get voltage-high; tests ALRT path
  for (uint8_t b = 0; b < 7; b++) {
    (void)fuel.clearAlertFlag(static_cast<uint8_t>(1U << b));
  }
  fuel.setAlertVoltages(minV, maxV);
  (void)fuel.getAlertStatus();
  Serial.print(F("MAX17048: alert if V < "));
  Serial.print(minV, 2);
  Serial.print(F(" V  OR  V > "));
  Serial.print(maxV, 2);
  Serial.print(F(" V  (intentional: any normal cell >> "));
  Serial.print(maxV, 2);
  Serial.println(F(" V should trip 'high' / ALRT)"));
  gFuelAmbiguousPrints = 0;
  gPhase = Phase::WaitFuel;
  gPhaseTime = millis();
  // Re-sync so the next LOW edge (or post-arm level) is attributed to fuel, not RTC.
  gPrevLow = digitalRead(raven::PIN_ALERT) == LOW;
}

void loop() {
  const bool alertLow = digitalRead(raven::PIN_ALERT) == LOW;
  digitalWrite(raven::PIN_LED_GREEN, alertLow ? HIGH : LOW);

  const bool edgeFalling = !gPrevLow && alertLow;
  gPrevLow = alertLow;

  switch (gPhase) {
  case Phase::WaitRtc: {
    if (edgeFalling) {
      Serial.println(F("PIN_ALERT: assert (LOW)"));
    }
    if (rtc.alarmFired(1)) {
      Serial.println(F("DS3231: alarm1 fired (A1F) — clearing /INT"));
      gDs3231IntOk = true;
      Serial.println(
          F("INDEPENDENT: DS3231 A1F was set (RTC /INT to gate asserted for this test)."));
      rtc.clearAlarm(1);
      ds3231ClearIntFlags();
      gPhase = Phase::RtcCleared;
      gPhaseTime = millis();
    } else if (millis() - gPhaseTime > kWaitRtcTimeoutMs) {
      Serial.println(F("Timeout: no DS3231 alarm. Check INTCN, time, and alarm."));
      gPhase = gMaxOk && kRunFuelTest ? Phase::ArmFuel : Phase::Done;
      gPhaseTime = millis();
    }
    break;
  }
  case Phase::RtcCleared:
    if (millis() - gPhaseTime > 300) {
      gPhase = Phase::ArmFuel;
    }
    break;
  case Phase::ArmFuel:
    tryArmFuel();
    break;
  case Phase::WaitFuel: {
    const uint8_t st = fuel.getAlertStatus();
    const bool vHi = (st & MAX1704X_ALERTFLAG_VOLTAGE_HIGH) != 0;
    const bool rtcClear = !rtc.alarmFired(1);
    if ((edgeFalling || (alertLow && vHi)) && vHi && rtcClear) {
      Serial.println(
          F("PIN_ALERT: assert (LOW) [fuel path — not DS3231: A1F is clear, VHi set]"));
      Serial.print(F("MAX17048: status=0x"));
      Serial.print(st, HEX);
      Serial.print(F("  (VOLTAGE_HIGH=0x"));
      Serial.print(MAX1704X_ALERTFLAG_VOLTAGE_HIGH, HEX);
      Serial.println(F(")"));
      gMax17048IntOk = true;
      Serial.println(
          F("INDEPENDENT: MAX17048 VHi + RTC not firing — /ALRT to gate for this test."));
      fuel.hibernate();
      gPhase = Phase::Done;
    } else if (edgeFalling && gFuelAmbiguousPrints < 2) {
      gFuelAmbiguousPrints++;
      Serial.println(
          F("PIN_ALERT: assert (LOW) [ambiguous: need VHi + A1F clear for fuel pass]"));
      Serial.print(F("  A1F still? "));
      Serial.print(rtc.alarmFired(1) ? F("yes (RTC)") : F("no"));
      Serial.print(F("  status=0x"));
      Serial.println(st, HEX);
    } else if (millis() - gPhaseTime > 20000) {
      Serial.println(
          F("No unambiguous fuel assert in window (normal if no pack or wrong thresholds)."));
      fuel.hibernate();
      gPhase = Phase::Done;
    }
    break;
  }
  case Phase::Done:
  default: {
    static bool printed = false;
    if (!printed) {
      printed = true;
      printFinalResult();
      Serial.println(F("Test finished. Re-flash or reset to run again."));
    }
    break;
  }
  }

  delay(20);
}
