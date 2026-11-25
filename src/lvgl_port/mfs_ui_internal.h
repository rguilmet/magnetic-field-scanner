#ifndef MFS_UI_INTERNAL_H
#define MFS_UI_INTERNAL_H

#include "lvgl_port.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t * mfs_title_label;
extern lv_obj_t * mfs_bg_arc_left;
extern lv_obj_t * mfs_bg_arc_right;
extern lv_obj_t * mfs_mag_arc;
extern lv_obj_t * mfs_compass_label_n;
extern lv_obj_t * mfs_compass_label_e;
extern lv_obj_t * mfs_compass_label_s;
extern lv_obj_t * mfs_compass_label_w;
extern lv_obj_t * mfs_compass_label_ne;
extern lv_obj_t * mfs_compass_label_se;
extern lv_obj_t * mfs_compass_label_sw;
extern lv_obj_t * mfs_compass_label_nw;
extern lv_obj_t * mfs_crosshair_dot;
extern lv_obj_t * mfs_mag_label;
extern lv_obj_t * mfs_declination_label;
extern lv_obj_t * mfs_elevation_label;
extern lv_obj_t * mfs_polarity_label;
extern lv_obj_t * mfs_nt_label;
extern lv_obj_t * mfs_scan_btn;
extern lv_obj_t * mfs_ip_label;
extern lv_obj_t * mfs_scan_label;
extern lv_obj_t * mfs_mute_btn;
extern lv_obj_t * mfs_mute_label;
extern lv_obj_t * mfs_wave_btn;
extern lv_obj_t * mfs_wave_label;
extern lv_obj_t * mfs_tare_btn;
extern lv_obj_t * mfs_tare_label;
extern lv_obj_t * mfs_wifi_btn;
extern lv_obj_t * mfs_wifi_label;
extern lv_obj_t * mfs_cc_label;
extern lv_obj_t * mfs_batt_label;
extern bool       mfs_is_calibrating;

void mfs_flush_pending_wifi_status(void);

#ifdef __cplusplus
}
#endif

#endif // MFS_UI_INTERNAL_H
