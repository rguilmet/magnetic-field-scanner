#pragma once
#include <Arduino.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
void get_formatted_timestamp(char* buffer, size_t max_len, bool include_ms);
void start_logging(void);
void stop_logging(void);
void start_calibration_logging(void);
void rename_calibration_file_to_stopped(void);
bool process_calibration_file(void);
void log_data(uint32_t timestamp, float voltage, float audio_gain, int cc, int32_t refX_raw, int32_t refY_raw, int32_t refZ_raw, int32_t tipX_raw, int32_t tipY_raw, int32_t tipZ_raw, float refX_cal, float refY_cal, float refZ_cal, float tipX_cal, float tipY_cal, float tipZ_cal, float calOffsetX, float calOffsetY, float calOffsetZ, float gradX, float gradY, float gradZ, float mag, float nT, float accX, float accY, float accZ, float gyrX, float gyrY, float gyrZ, int16_t imu_temp, float freq, bool is_muted, float qw, float qx, float qy, float qz, float azimuth, float elevation, float declination);
#ifdef __cplusplus
}
#endif
