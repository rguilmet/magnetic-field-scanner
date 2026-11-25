#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Field_Instrument_Core.h>
#include "../settings_manager/settings_manager.h"
#include "../data_logger/data_logger.h"
#include "mfs_ui_internal.h"

static portMUX_TYPE s_wifi_status_mux = portMUX_INITIALIZER_UNLOCKED;
static char         s_pending_wifi_status[64] = "";
static bool         s_wifi_status_dirty = false;

bool mfs_lvgl_lock(int timeout_ms) {
    return fic_lvgl_lock(timeout_ms);
}

void mfs_lvgl_unlock(void) {
    fic_lvgl_unlock();
}

void update_wifi_ip_label(const char * ip_str) {
    taskENTER_CRITICAL(&s_wifi_status_mux);
    if (ip_str != NULL) {
        snprintf(s_pending_wifi_status, sizeof(s_pending_wifi_status), "%s", ip_str);
    } else {
        s_pending_wifi_status[0] = '\0';
    }
    s_wifi_status_dirty = true;
    taskEXIT_CRITICAL(&s_wifi_status_mux);
}

void mfs_flush_pending_wifi_status(void) {
    char status_buf[64] = "";
    bool has_update = false;

    taskENTER_CRITICAL(&s_wifi_status_mux);
    if (s_wifi_status_dirty) {
        snprintf(status_buf, sizeof(status_buf), "%s", s_pending_wifi_status);
        s_wifi_status_dirty = false;
        has_update = true;
    }
    taskEXIT_CRITICAL(&s_wifi_status_mux);

    if (!has_update) {
        return;
    }

    if (mfs_ip_label != NULL) {
        lv_label_set_text(mfs_ip_label, status_buf);
    }
    if (mfs_wifi_btn != NULL && mfs_wifi_label != NULL) {
        bool active = fic_web_portal_is_active() || fic_web_portal_is_starting();
        if (active) {
            lv_obj_add_state(mfs_wifi_btn, LV_STATE_CHECKED);
            lv_label_set_text(mfs_wifi_label, "Stop Wi-Fi");
        } else {
            lv_obj_clear_state(mfs_wifi_btn, LV_STATE_CHECKED);
            lv_label_set_text(mfs_wifi_label, "Start Wi-Fi");
        }
    }
}

static void format_with_commas(int32_t num, char * out_buf) {
    char temp[32];
    snprintf(temp, sizeof(temp), "%ld", (long)num);

    int len = (int)strlen(temp);
    int out_idx = 0;

    int start_idx = 0;
    if (temp[0] == '-') {
        out_buf[out_idx++] = '-';
        start_idx = 1;
    }

    for (int i = start_idx; i < len; i++) {
        out_buf[out_idx++] = temp[i];
        int remaining_digits = len - i - 1;
        if (remaining_digits > 0 && remaining_digits % 3 == 0) {
            out_buf[out_idx++] = ',';
        }
    }
    out_buf[out_idx] = '\0';
}

void update_detector_ui(const UIData *data) {
    if (!mfs_lvgl_lock(-1)) {
        return;
    }

    mfs_flush_pending_wifi_status();

    // Update Time Labels
    if (ui_date_label && ui_time_label) {
        char ts[64];
        get_formatted_timestamp(ts, sizeof(ts), false);
        char d_str[16];
        char t_str[16];
        // Format is YYYY-MM-DD_HH-MM-SS+Offset
        snprintf(d_str, sizeof(d_str), "%c%c/%c%c/%c%c%c%c", ts[5], ts[6], ts[8], ts[9], ts[0], ts[1], ts[2], ts[3]);
        snprintf(t_str, sizeof(t_str), "%c%c:%c%c:%c%c", ts[11], ts[12], ts[14], ts[15], ts[17], ts[18]);
        lv_label_set_text(ui_date_label, d_str);
        lv_label_set_text(ui_time_label, t_str);
    }

    if (cal_progress_bar != NULL && cal_status_label != NULL) {
        if (data->cal_progress > 0 && data->cal_progress < 100) {
            lv_bar_set_value(cal_progress_bar, data->cal_progress, LV_ANIM_ON);
            int remain = (100 - data->cal_progress) * 60 / 100;
            char cal_buf[64];
            snprintf(cal_buf, sizeof(cal_buf), "Capturing Data: %d%% (%ds left)", data->cal_progress, remain);
            lv_label_set_text(cal_status_label, cal_buf);
        } else if (data->cal_progress >= 100) {
            lv_bar_set_value(cal_progress_bar, 100, LV_ANIM_ON);
            lv_label_set_text(cal_status_label, "Calculating...");
        }
    }

    if (mfs_title_label != NULL) {
        if (data->auto_tare_on) {
            lv_label_set_text(mfs_title_label, "Magnetic Field\n(AUTO)");
            lv_obj_set_style_text_color(mfs_title_label, lv_color_hex(0x00ff00), LV_PART_MAIN);
        } else if (data->tare_active) {
            lv_label_set_text(mfs_title_label, "Magnetic Field\n(TARED)");
            lv_obj_set_style_text_color(mfs_title_label, lv_color_hex(0xff0000), LV_PART_MAIN);
        } else {
            lv_label_set_text(mfs_title_label, "Magnetic Field\nScanner");
            lv_obj_set_style_text_color(mfs_title_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        }
    }

    if (mfs_mag_arc != NULL && mfs_mag_label != NULL) {
        float nt_val = data->nt;
        if (nt_val < 0.0f) nt_val = 0.0f;

        float log_val = log10f((nt_val / 100.0f) + 1.0f);
        int32_t arc_val = (int32_t)(log_val * 100.0f);

        if (data->trueZ > 0.0f) {
            arc_val = -arc_val;
        }

        lv_arc_set_range(mfs_mag_arc, -400, 400);
        lv_arc_set_value(mfs_mag_arc, arc_val);

        static int last_polarity_state = 0; // 1=Right/Red, 2=Left/Blue
        int current_polarity_state = (arc_val < 0) ? 1 : 2;

        if (current_polarity_state != last_polarity_state) {
            if (current_polarity_state == 1) {
                lv_obj_set_style_arc_color(mfs_mag_arc, lv_color_hex(0xff3333), LV_PART_INDICATOR);
            } else {
                lv_obj_set_style_arc_color(mfs_mag_arc, lv_color_hex(0x3388ff), LV_PART_INDICATOR);
            }

            lv_obj_set_style_text_color(mfs_mag_label, lv_color_hex(0xffffff), LV_PART_MAIN);
            if (mfs_nt_label != NULL) lv_obj_set_style_text_color(mfs_nt_label, lv_color_hex(0xffffff), LV_PART_MAIN);

            last_polarity_state = current_polarity_state;
        }

        char formatted_nt[32];
        format_with_commas((int32_t)data->nt, formatted_nt);
        lv_label_set_text(mfs_mag_label, formatted_nt);

        if (mfs_nt_label != NULL) {
            char int_buf[32];
            format_with_commas((int32_t)data->mag, int_buf);
            lv_label_set_text(mfs_nt_label, int_buf);
        }
    }

    if (mfs_batt_label) {
        char batt_buf[32];
        snprintf(batt_buf, sizeof(batt_buf), "Bat: %.2fV", data->battery_voltage);
        lv_label_set_text(mfs_batt_label, batt_buf);
    }

    if (mfs_crosshair_dot != NULL) {
        float squelch_nt = 20.0f;
        if (data->nt < squelch_nt) {
            lv_obj_set_hidden(mfs_crosshair_dot, true);
        } else {
            lv_obj_set_hidden(mfs_crosshair_dot, false);

            float H_mag = sqrtf(data->gradX * data->gradX + data->gradY * data->gradY);
            float radius = 70.0f * powf(squelch_nt / data->nt, 0.33333f);
            if (radius > 70.0f) radius = 70.0f;

            float px = 0.0f;
            float py = 0.0f;

            if (H_mag > 0.1f) {
                float normX = data->gradX / H_mag;
                float normY = data->gradY / H_mag;
                px = normX * radius;
                py = normY * radius;
            }

            if (data->trueZ > 15.0f) {
                lv_obj_set_style_bg_color(mfs_crosshair_dot, lv_color_hex(0xff3333), LV_PART_MAIN);
            } else if (data->trueZ < -15.0f) {
                lv_obj_set_style_bg_color(mfs_crosshair_dot, lv_color_hex(0x3388ff), LV_PART_MAIN);
            } else {
                lv_obj_set_style_bg_color(mfs_crosshair_dot, lv_color_hex(0x00ff00), LV_PART_MAIN);
            }

            lv_obj_align(mfs_crosshair_dot, LV_ALIGN_CENTER, (int32_t)px, (int32_t)py);
        }

        if (mfs_declination_label != NULL) {
            if (current_settings.mag_declination_deg == 0.0f) {
                lv_label_set_text(mfs_declination_label, "MAG");
            } else {
                lv_label_set_text(mfs_declination_label, "TN");
            }
        }

        if (mfs_elevation_label != NULL) {
            char elev_buf[32];
            snprintf(elev_buf, sizeof(elev_buf), "%+05.1f deg", data->elevation);
            lv_label_set_text(mfs_elevation_label, elev_buf);
        }

        if (mfs_compass_label_n != NULL) {
            float cr = 58.0f;
            float yaw_rad = data->azimuth * 3.14159f / 180.0f;

            lv_obj_align(mfs_compass_label_n,  LV_ALIGN_CENTER, (int)(sinf(-yaw_rad) * cr),            (int)(-cosf(-yaw_rad) * cr));
            lv_obj_align(mfs_compass_label_e,  LV_ALIGN_CENTER, (int)(sinf(1.5708f - yaw_rad) * cr),  (int)(-cosf(1.5708f - yaw_rad) * cr));
            lv_obj_align(mfs_compass_label_s,  LV_ALIGN_CENTER, (int)(sinf(3.14159f - yaw_rad) * cr), (int)(-cosf(3.14159f - yaw_rad) * cr));
            lv_obj_align(mfs_compass_label_w,  LV_ALIGN_CENTER, (int)(sinf(4.71239f - yaw_rad) * cr), (int)(-cosf(4.71239f - yaw_rad) * cr));
            lv_obj_align(mfs_compass_label_ne, LV_ALIGN_CENTER, (int)(sinf(0.785398f - yaw_rad) * cr),(int)(-cosf(0.785398f - yaw_rad) * cr));
            lv_obj_align(mfs_compass_label_se, LV_ALIGN_CENTER, (int)(sinf(2.356194f - yaw_rad) * cr),(int)(-cosf(2.356194f - yaw_rad) * cr));
            lv_obj_align(mfs_compass_label_sw, LV_ALIGN_CENTER, (int)(sinf(3.926991f - yaw_rad) * cr),(int)(-cosf(3.926991f - yaw_rad) * cr));
            lv_obj_align(mfs_compass_label_nw, LV_ALIGN_CENTER, (int)(sinf(5.497787f - yaw_rad) * cr),(int)(-cosf(5.497787f - yaw_rad) * cr));
        }
    }

    if (mfs_polarity_label != NULL) {
        if (data->is_pin) {
            lv_label_set_text(mfs_polarity_label, "FERROUS PIN");
        } else {
            lv_label_set_text(mfs_polarity_label, " ");
        }
    }

    mfs_lvgl_unlock();
}
