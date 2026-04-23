#include "ULPManager.h"

// counts all state changes (LOW->HIGH, HIGH->LOW)
// divide by 2 for single transition type
ulp_insn_t ulp_program[20]; // Pre-allocate space for the program

ULPManager::ULPManager() : _initialized(false), _sensorPin(GPIO_NUM_16), _rtcGpioIndex(16)
{
    updateRtcGpioIndex();
}

void ULPManager::setSensorPin(gpio_num_t pin)
{
    _sensorPin = pin;
    updateRtcGpioIndex();
}

void ULPManager::updateRtcGpioIndex()
{
    _rtcGpioIndex = rtc_io_number_get(_sensorPin);
}

void ULPManager::begin()
{
    // init GPIO for ULP to monitor
    rtc_gpio_init(_sensorPin);
    rtc_gpio_set_direction(_sensorPin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(_sensorPin);    // enable the pull-up resistor
    rtc_gpio_pulldown_dis(_sensorPin); // disable the pull-down resistor
    rtc_gpio_hold_en(_sensorPin);      // required to maintain pull-up
    _initialized = true;
    Serial.println("  ULP: initialization complete");
}

void ULPManager::start()
{
    Serial.println("  ULP: starting program");

    // Build the ULP program dynamically with the current RTC GPIO index
    const ulp_insn_t program_template[] = {
        // Initialize transition counter and previous state
        I_MOVI(R3, 0), // R3 <- 0 (reset the transition counter)
        I_RD_REG(RTC_GPIO_IN_REG, _rtcGpioIndex + RTC_GPIO_IN_NEXT_S, _rtcGpioIndex + RTC_GPIO_IN_NEXT_S),
        I_MOVR(R2, R0), // R2 <- R0, initial state is variable due to latching sensor

        // Main loop
        M_LABEL(1),

        // Read RTC GPIO with dynamic RTC offset
        I_RD_REG(RTC_GPIO_IN_REG, _rtcGpioIndex + RTC_GPIO_IN_NEXT_S, _rtcGpioIndex + RTC_GPIO_IN_NEXT_S),

        // Save the current state in a temporary register (R1)
        I_MOVR(R1, R0), // R1 <- R0 (store current GPIO state temporarily)

        // Compare current state (R1) with previous state (R2)
        I_SUBR(R0, R1, R2), // R0 = current state (R1) - previous state (R2)
        I_BL(5, 1),         // If R0 == 0 (no state change), skip instructions
        I_ADDI(R3, R3, 1),  // Increment R3 by 1 (transition detected)
        I_MOVR(R2, R1),     // R2 <- R1 (store the current state for the next iteration)

        // Store the transition counter
        I_MOVI(R1, EDGE_COUNT), // Set R1 to address RTC_SLOW_MEM[1]
        I_ST(R3, R1, 0),        // Store it in RTC_SLOW_MEM

        // RTC clock on the ESP32-S3 is 17.5MHz, delay 0xFFFF = 3.74 ms
        I_DELAY(0xFFFF), // debounce
        // I_DELAY(0xFFFF), // debounce
        // I_DELAY(0xFFFF), // debounce
        // I_DELAY(0xFFFF), // debounce
        // I_DELAY(0xFFFF), // debounce
        // I_DELAY(0xFFFF), // debounce

        M_BX(1), // Loop back to label 1
    };

    // Copy the template program to our working buffer
    memcpy(ulp_program, program_template, sizeof(program_template));

    // Load and start the program
    size_t size = sizeof(program_template) / sizeof(ulp_insn_t);
    esp_err_t err = ulp_process_macros_and_load(PROG_START, ulp_program, &size);
    if (err != ESP_OK)
    {
        Serial.printf("  ULP: program load error: %d\n", err);
        return;
    }

    err = ulp_run(PROG_START);
    if (err != ESP_OK)
    {
        Serial.printf("  ULP: start error: %d\n", err);
        return;
    }
    Serial.println("  ULP: program started");
}

uint16_t ULPManager::getEdgeCount()
{
    uint16_t count = (uint16_t)(RTC_SLOW_MEM[EDGE_COUNT] & 0xFFFF);
    Serial.printf("  ULP: current edge count: %d\n", count);
    return count;
}

void ULPManager::clearEdgeCount()
{
    Serial.println("  ULP: clearing edge count");
    RTC_SLOW_MEM[EDGE_COUNT] = 0;
    Serial.printf("  ULP: verified count is now: %d\n", (uint16_t)(RTC_SLOW_MEM[EDGE_COUNT] & 0xFFFF));
}