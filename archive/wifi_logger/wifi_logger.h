
#ifndef WIFI_LOGGER_H
#define WIFI_LOGGER_H

#include <Arduino.h>

struct CalibrationConfig {
    float tip_hard[3];
    float tip_soft[3][3];
    float ref_hard[3];
    float ref_soft[3][3];
    float imu_rotation_deg;
};

struct SystemSettings {
    int16_t minutesOffsetToUTC;
    uint8_t brightness;
    uint8_t volume;
    uint8_t audio_gain;
    uint16_t cycle_count;
    bool is_muted;
    bool auto_tare_on;
    float audio_base_freq;
    float audio_max_freq;
    uint8_t audio_waveform; // 0=Square, 1=Triangle, 2=Sine
    char wifi_ssid[33];
    char wifi_password[65];
    bool enable_serial_logging;
    char timezone_label[16];
};

extern struct CalibrationConfig cal_config;
extern struct SystemSettings current_settings;
extern volatile bool settings_dirty;
extern volatile uint32_t last_settings_change_ms;
extern bool sd_card_mounted;

#ifdef __cplusplus
extern "C" {
#endif

void init_wifi_logger(void);
void load_settings(void);
void save_settings(void);
void get_formatted_timestamp(char* buffer, size_t max_len, bool include_ms);
void load_calibration(void);
void save_calibration(float ref_hard[3], float ref_soft[3][3], float tip_hard[3], float tip_soft[3][3]);
void start_logging(void);
void start_calibration_logging(void);
void stop_logging(void);
bool process_calibration_file(void);
void log_data(uint32_t timestamp, float gain, int cc, int32_t refX_raw, int32_t refY_raw, int32_t refZ_raw, int32_t tipX_raw, int32_t tipY_raw, int32_t tipZ_raw, int32_t refX_cal, int32_t refY_cal, int32_t refZ_cal, int32_t tipX_cal, int32_t tipY_cal, int32_t tipZ_cal, int32_t calOffsetX, int32_t calOffsetY, int32_t calOffsetZ, int32_t gradX, int32_t gradY, int32_t gradZ, float mag, float nT, float accX, float accY, float accZ, float gyrX, float gyrY, float gyrZ, float freq, bool is_muted);
void start_wifi(void);
void stop_wifi(void);
void handle_wifi_server(void);
void take_screenshot_to_sd(void);

#ifdef __cplusplus
}
#endif

#endif

