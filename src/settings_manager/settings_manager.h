#pragma once
#include <Arduino.h>
struct CalibrationConfig { float tip_hard[3]; float tip_soft[3][3]; float ref_hard[3]; float ref_soft[3][3]; float imu_rotation_deg; };
struct SystemSettings { int16_t minutesOffsetToUTC; uint8_t brightness; uint8_t volume; uint8_t audio_gain; uint16_t cycle_count; bool is_muted; bool auto_tare_on; float audio_base_freq; float audio_max_freq; uint8_t audio_waveform; char wifi_ssid[33]; char wifi_password[65]; bool enable_serial_logging; char timezone_label[16]; };
extern struct CalibrationConfig cal_config;
extern struct SystemSettings current_settings;
extern volatile bool settings_dirty;
extern volatile uint32_t last_settings_change_ms;
extern bool sd_card_mounted;
#ifdef __cplusplus
extern "C" {
#endif
void load_settings(void);
void save_settings(void);
void load_calibration(void);
void save_calibration(float ref_hard[3], float ref_soft[3][3], float tip_hard[3], float tip_soft[3][3]);
void init_wifi_logger(void);
#ifdef __cplusplus
}
#endif
