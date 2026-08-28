#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void set_rm3100_cycle_count(uint16_t count);
void toggle_scanning(bool scanning);
void toggle_mute(bool muted);
void set_audio_gain(int gain);
void set_audio_volume(int vol);
void calibrate_sensors(void);
void reset_calibration(void);
void set_auto_tare(bool enable);
void trigger_manual_tare(void);
void reset_tare(void);

#ifdef __cplusplus
}
#endif

