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
bool process_calibration_file(void);
void log_data(uint32_t timestamp, float voltage, float audio_gain, int cc, int32_t refX_raw, int32_t refY_raw, int32_t refZ_raw, int32_t tipX_raw, int32_t tipY_raw, int32_t tipZ_raw, int32_t refX_cal, int32_t refY_cal, int32_t refZ_cal, int32_t tipX_cal, int32_t tipY_cal, int32_t tipZ_cal, int32_t calOffsetX, int32_t calOffsetY, int32_t calOffsetZ, int32_t gradX, int32_t gradY, int32_t gradZ, float mag, float nT, float accX, float accY, float accZ, float gyrX, float gyrY, float gyrZ, float freq, bool is_muted, float qw, float qx, float qy, float qz);
#ifdef __cplusplus
}
#endif

