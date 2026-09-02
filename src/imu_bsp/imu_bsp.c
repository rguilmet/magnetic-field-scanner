#include "imu_bsp.h"
#include "../i2c_bsp/i2c_bsp.h"
#include <stdint.h>
#include <esp_err.h>

extern i2c_master_dev_handle_t imu_dev_handle;

void imu_init(void) {
    uint8_t ctrl_buf;
    // Enable Address Auto Increment
    ctrl_buf = 0x60;
    i2c_write_buff(imu_dev_handle, 0x02, &ctrl_buf, 1);
    
    // Accel Config (4g, 500Hz)
    ctrl_buf = 0x13; 
    i2c_write_buff(imu_dev_handle, 0x03, &ctrl_buf, 1);
    
    // Gyro Config (512dps, 500Hz)
    ctrl_buf = 0x53;
    i2c_write_buff(imu_dev_handle, 0x04, &ctrl_buf, 1);
    
    // Enable Sensors (SYS_EN=1, ACC_EN=1, GYR_EN=1)
    // BUG FIX: 0x03 only enabled SYS and ACC. The Gyroscope was physically disabled and outputting flatline noise!
    // 0x07 (0b0111) is required to turn on the MEMS Gyroscope oscillator.
    ctrl_buf = 0x07;
    i2c_write_buff(imu_dev_handle, 0x08, &ctrl_buf, 1);
}

void imu_read(float *acc, float *gyr, int16_t *temp) {
    uint8_t raw[14];
    if (i2c_read_buff(imu_dev_handle, 0x33, raw, 14) == ESP_OK) {
        int16_t t_raw = (raw[1] << 8) | raw[0];
        int16_t ax = (raw[3] << 8) | raw[2];
        int16_t ay = (raw[5] << 8) | raw[4];
        int16_t az = (raw[7] << 8) | raw[6];
        int16_t gx = (raw[9] << 8) | raw[8];
        int16_t gy = (raw[11] << 8) | raw[10];
        int16_t gz = (raw[13] << 8) | raw[12];
        
        *temp = t_raw;
        
        // 4g range -> 1g = 8192 LSB
        acc[0] = (float)ax / 8192.0f;
        acc[1] = (float)ay / 8192.0f;
        acc[2] = (float)az / 8192.0f;
        
        // 512dps -> 1dps = 64 LSB
        gyr[0] = (float)gx / 64.0f;
        gyr[1] = (float)gy / 64.0f;
        gyr[2] = (float)gz / 64.0f;
    } else {
        *temp = 0;
        acc[0] = acc[1] = acc[2] = 0.0f;
        gyr[0] = gyr[1] = gyr[2] = 0.0f;
    }
}
