#include "Display.h"

Display::Display() : u8g2(U8G2_R0, U8X8_PIN_NONE)
{
}

void Display::begin()
{
  u8g2.beginSimple();
}

void Display::showMeasurement(uint16_t co2, float temp, float rh, String message = "")
{
  u8g2.firstPage();
  do
  {
    // Display CO2 measurement in big font
    u8g2.setFont(u8g2_font_logisoso32_tn);
    String co2Str = (co2 == 0) ? "--" : String(co2);
    u8g2.drawStr(90 - u8g2.getStrWidth(co2Str.c_str()), 56, co2Str.c_str());

    u8g2.setFont(u8g2_font_9x18_tr);
    u8g2.drawStr(94, 56 - 16, "CO2");
    u8g2.drawStr(94, 56, "ppm");

    if (message != "")
    {
      u8g2.setFont(u8g2_font_9x18_tr);
      u8g2.drawStr(0, 12, message.c_str());
    }
    else
    {
      // temp and rh values top
      u8g2.setFont(u8g2_font_9x18_tr); // smaller 14px font

      if (temp != NO_VALUE)
      {
        String tempStr = String(temp, 1);
        u8g2.drawStr(0, 12, tempStr.c_str());
        int tempStrWidth = u8g2.getStrWidth(tempStr.c_str());

        // Draw degree symbol
        u8g2.drawCircle(tempStrWidth + 3, 4, 2);
        u8g2.drawStr(tempStrWidth + 8, 12, "C");
      }
      else
      {
        u8g2.drawStr(0, 12, "--");
      }

      String rhStr = (rh != NO_VALUE) ? String(rh, 1) + "%" : "--";
      u8g2.drawStr(128 - u8g2.getStrWidth(rhStr.c_str()), 12, rhStr.c_str());
    }

  } while (u8g2.nextPage());
  u8g2.refreshDisplay();
}

void Display::turnOn()
{
  u8g2.setPowerSave(0);
}

void Display::turnOff()
{
  u8g2.setPowerSave(1);
}
