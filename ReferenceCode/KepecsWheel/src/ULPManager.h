#ifndef ULP_MANAGER_H
#define ULP_MANAGER_H

#include <Arduino.h>
#include "esp32s3/ulp.h"
#include "driver/rtc_io.h"
#include "soc/rtc_io_reg.h"
#include "ulp_common.h"

enum
{
    EDGE_COUNT,
    PROG_START // Program start address
};

class ULPManager
{
public:
    ULPManager();
    void begin();
    void start();
    uint16_t getEdgeCount();
    void clearEdgeCount();
    void setSensorPin(gpio_num_t pin);

private:
    bool _initialized;
    gpio_num_t _sensorPin;
    uint8_t _rtcGpioIndex;
    void updateRtcGpioIndex();
};

#endif // ULP_MANAGER_H
