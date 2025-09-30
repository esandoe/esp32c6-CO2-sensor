#ifndef DISPLAY_H
#define DISPLAY_H

#include <U8g2lib.h>
#include "Arduino.h"

#define NO_VALUE -123456789.0f

class Display {
private:
    U8G2_SSD1315_128X64_NONAME_1_HW_I2C u8g2;

public:
    Display();
    
    void begin();
    void showMeasurement(uint16_t co2, float temp, float rh, String message = "");
    void turnOn();
    void turnOff();
};

#endif