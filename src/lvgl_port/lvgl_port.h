#ifndef LVGL_PORT_H
#define LVGL_PORT_H



#ifdef __cplusplus
extern "C" {
#endif


#include <stdbool.h>

typedef struct {
    float mag;
    float nt;
    float gradX;
    float gradY;
    float trueZ;
    bool is_pin;
    bool tare_active;
    bool auto_tare_on;
    float battery_voltage;
    int cal_progress;
} UIData;

void lvgl_port_init(void);

// Expose UI functions for the Magnetic Field Detector
void create_detector_ui(void);
void update_detector_ui(const UIData *data);
void update_wifi_ip_label(const char * ip_str);
void show_calibration_result_msg(bool success);
bool mfs_lvgl_lock(int timeout_ms);
void mfs_lvgl_unlock(void);

#ifdef __cplusplus
}
#endif



#endif










