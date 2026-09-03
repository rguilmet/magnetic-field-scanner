#ifndef USER_CONFIG_H
#define USER_CONFIG_H

//spi & i2c handle
#define LCD_HOST SPI3_HOST

// I2C1 Bus (Touch Panel)
#define MFS_PIN_I2C1_TOUCH_SCL (GPIO_NUM_18)
#define MFS_PIN_I2C1_TOUCH_SDA (GPIO_NUM_17)

// I2C0 Bus (Sensors)
#define MFS_PIN_I2C0_SCL (GPIO_NUM_48)
#define MFS_PIN_I2C0_SDA (GPIO_NUM_47)

// SPI Bus (SD Card)
#define MFS_PIN_SD_CS   38
#define MFS_PIN_SD_MOSI 39
#define MFS_PIN_SD_MISO 40
#define MFS_PIN_SD_SCLK 41

#define CALIBRATION_POINTS 3000

// Sensor Magic Numbers & Tuning
#define MFS_SLEW_RATE_THRESHOLD     800     // Max LSB jump per 2.5ms frame (EMI filter)
#define MFS_MAX_GLITCH_MAGNITUDE    250000   // Absolute max LSB allowed (EMI filter). CC=3200 hits ~80k-120k LSB.
#define MFS_AUTO_TARE_THRESHOLD     50.0f   // Max LSB jump for auto-tare to track
#define MFS_EMA_ALPHA               0.995f  // Auto-tare EMA filter weight
#define MFS_NT_CONVERSION_FACTOR    0.38f   // LSB/uT/CC constant for RM3100
#define MFS_AUDIO_SQUELCH_NT        20.0f   // nT threshold before audio kicks in

// RM3100 Data Ready Interrupts
#define MFS_PIN_RM3100_TIP_DRDY 1
#define MFS_PIN_RM3100_REF_DRDY 2
#define MFS_PIN_RM3100_MID_DRDY 5


//  DISP
#define MFS_PIN_NUM_LCD_CS     (GPIO_NUM_9) 
#define MFS_PIN_NUM_LCD_PCLK   (GPIO_NUM_10)
#define MFS_PIN_NUM_LCD_DATA0  (GPIO_NUM_11)
#define MFS_PIN_NUM_LCD_DATA1  (GPIO_NUM_12)
#define MFS_PIN_NUM_LCD_DATA2  (GPIO_NUM_13)
#define MFS_PIN_NUM_LCD_DATA3  (GPIO_NUM_14)
#define MFS_PIN_NUM_LCD_TE     (GPIO_NUM_21)
#define MFS_PIN_NUM_LCD_RST    (-1)
#define MFS_PIN_NUM_BK_LIGHT   (GPIO_NUM_42)
#define MFS_PIN_NUM_EXIO_INT   (GPIO_NUM_8)

#define MFS_PIN_NUM_BAT_ADC    (GPIO_NUM_4)
#define MFS_PIN_NUM_SYS_OUT    (GPIO_NUM_16)

#define MFS_EXIO_PIN_TOUCH_INT (1ULL << 0)
#define MFS_EXIO_PIN_BL_EN     (1ULL << 1)
#define MFS_EXIO_PIN_IMU_INT1  (1ULL << 2)
#define MFS_EXIO_PIN_IMU_INT2  (1ULL << 3)
#define MFS_EXIO_PIN_RTC_INT   (1ULL << 4)
#define MFS_EXIO_PIN_LCD_RST   (1ULL << 5)
#define MFS_EXIO_PIN_SYS_EN    (1ULL << 6)
#define MFS_EXIO_PIN_NS_MODE   (1ULL << 7)

#define I2C_TOUCH_ADDR                    0x3b
#define MFS_PIN_NUM_TOUCH_RST         (-1)
#define MFS_PIN_NUM_TOUCH_INT         (-1)

#define LVGL_TICK_PERIOD_MS    5
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 5
#define LVGL_TASK_STACK_SIZE   (8 * 1024)
#define LVGL_TASK_PRIORITY     2

/*bl test*/
#define Backlight_Testing 0

/*ADDR*/
#define MFS_RTC_ADDR 0x51

#define MFS_IMU_ADDR 0x6b

#define USER_DISP_ROT_90    1
#define USER_DISP_ROT_NONO  0
#define FIRMWARE_VERSION "v5.1.1"

#define Rotated USER_DISP_ROT_NONO   

#define MFS_LCD_H_RES 172   
#define MFS_LCD_V_RES 640

#define LCD_NOROT_HRES     172
#define LCD_NOROT_VRES     640
#define LVGL_DMA_BUFF_LEN (LCD_NOROT_HRES * 64 * 2)
#define LVGL_SPIRAM_BUFF_LEN (MFS_LCD_H_RES * MFS_LCD_V_RES * 2)

#endif

































