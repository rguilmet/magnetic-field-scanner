#include <stdio.h>
#include <Wire.h>
#include "rtc_bsp.h"
#include "esp_log.h"
#include "driver/i2c_master.h"


SensorPCF85063 rtc;


#include "../i2c_bsp/i2c_bsp.h"

bool rtc_i2c_custom_cb(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len, bool writeReg, bool isWrite)
{
    int reg_addr = writeReg ? reg : -1;
    if (isWrite) {
        return i2c_write_buff(rtc_dev_handle, reg_addr, buf, len) == ESP_OK;
    } else {
        return i2c_read_buff(rtc_dev_handle, reg_addr, buf, len) == ESP_OK;
    }
}

void rtc_init(void)
{
  if (rtc_dev_handle == NULL) {
      Serial.println("RTC I2C Handle not initialized yet!");
      return;
  }
  
  if (!rtc.begin(rtc_i2c_custom_cb))
  {
    Serial.println("Failed to find PCF85063 - check your wiring!");
  } else {
    Serial.println("PCF85063 RTC initialized successfully via custom callback.");
  }
}

void i2c_rtc_setTime(uint16_t year,uint8_t month,uint8_t day,uint8_t hour,uint8_t minute,uint8_t second)
{
  rtc.setDateTime(year, month, day, hour, minute, second);
}

RtcDateTime_t i2c_rtc_get(void)
{
  RtcDateTime_t time;
  RTC_DateTime datetime = rtc.getDateTime();
  time.year = datetime.getYear();
  time.month = datetime.getMonth();
  time.day = datetime.getDay();
  time.hour = datetime.getHour();
  time.minute = datetime.getMinute();
  time.second = datetime.getSecond();
  time.week = datetime.getWeek();
  return time;
}
