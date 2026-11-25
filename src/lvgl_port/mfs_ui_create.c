#include <stdio.h>
#include <Arduino.h>
#include <Field_Instrument_Core.h>
#include "../../user_config.h"
#include "../settings_manager/settings_manager.h"
#include "../data_logger/data_logger.h"
#include "../mfs_api.h"
#include "mfs_ui_internal.h"

extern void init_backlight_pwm(void);
extern void set_backlight_pwm(uint8_t brightness);
extern void start_on_wand_calibration(void);
extern void stop_on_wand_calibration(void);

lv_obj_t * mfs_title_label = NULL;
lv_obj_t * mfs_bg_arc_left = NULL;
lv_obj_t * mfs_bg_arc_right = NULL;
lv_obj_t * mfs_mag_arc = NULL;
lv_obj_t * mfs_compass_label_n = NULL;
lv_obj_t * mfs_compass_label_e = NULL;
lv_obj_t * mfs_compass_label_s = NULL;
lv_obj_t * mfs_compass_label_w = NULL;
lv_obj_t * mfs_compass_label_ne = NULL;
lv_obj_t * mfs_compass_label_se = NULL;
lv_obj_t * mfs_compass_label_sw = NULL;
lv_obj_t * mfs_compass_label_nw = NULL;
lv_obj_t * mfs_crosshair_dot = NULL;
lv_obj_t * mfs_mag_label = NULL;
lv_obj_t * mfs_declination_label = NULL;
lv_obj_t * mfs_elevation_label = NULL;
lv_obj_t * mfs_polarity_label = NULL;
lv_obj_t * mfs_nt_label = NULL;
lv_obj_t * mfs_scan_btn = NULL;
lv_obj_t * mfs_ip_label = NULL;
lv_obj_t * mfs_scan_label = NULL;
lv_obj_t * mfs_mute_btn = NULL;
lv_obj_t * mfs_mute_label = NULL;
lv_obj_t * mfs_wave_btn = NULL;
lv_obj_t * mfs_wave_label = NULL;
lv_obj_t * mfs_tare_btn = NULL;
lv_obj_t * mfs_tare_label = NULL;
lv_obj_t * mfs_wifi_btn = NULL;
lv_obj_t * mfs_wifi_label = NULL;
lv_obj_t * mfs_cc_label = NULL;
lv_obj_t * mfs_batt_label = NULL;

lv_obj_t * cal_progress_bar = NULL;
lv_obj_t * cal_status_label = NULL;
lv_obj_t * full_cal_btn = NULL;
lv_obj_t * full_cal_label = NULL;
lv_obj_t * ui_date_label = NULL;
lv_obj_t * ui_time_label = NULL;
lv_obj_t * main_tv = NULL;

static int main_tare_state = 0; // 0=RAW, 1=TARE, 2=AUTO
bool mfs_is_calibrating = false;
static lv_timer_t * cal_msg_timer = NULL;

static void mark_settings_dirty(void) {
    last_settings_change_ms = millis();
    settings_dirty = true;
}

static void wifi_status_poll_timer_cb(lv_timer_t * timer) {
    (void)timer;
    mfs_flush_pending_wifi_status();
}

static void vol_slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    int vol = lv_slider_get_value(slider);
    current_settings.volume = vol;
    mark_settings_dirty();
    set_audio_volume(vol);
}

static void gain_slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    int gain = lv_slider_get_value(slider);
    current_settings.audio_gain = gain;
    mark_settings_dirty();
    set_audio_gain(gain);
}

static void scan_btn_event_cb(lv_event_t * e) {
    (void)e;
    bool is_scanning = lv_obj_has_state(mfs_scan_btn, LV_STATE_CHECKED);
    if (is_scanning) {
        lv_obj_clear_state(mfs_scan_btn, LV_STATE_CHECKED);
        lv_label_set_text(mfs_scan_label, LV_SYMBOL_PAUSE);
        is_scanning = false;
    } else {
        lv_obj_add_state(mfs_scan_btn, LV_STATE_CHECKED);
        lv_label_set_text(mfs_scan_label, LV_SYMBOL_PLAY);
        is_scanning = true;
    }
    toggle_scanning(is_scanning);
}

static void wave_btn_event_cb(lv_event_t * e) {
    (void)e;
    current_settings.audio_waveform = (current_settings.audio_waveform + 1) % 4;
    if (current_settings.audio_waveform == 0) lv_label_set_text(mfs_wave_label, "SQR");
    else if (current_settings.audio_waveform == 1) lv_label_set_text(mfs_wave_label, "TRI");
    else if (current_settings.audio_waveform == 2) lv_label_set_text(mfs_wave_label, "SIN");
    else lv_label_set_text(mfs_wave_label, "GCM");
    mark_settings_dirty();
}

static void mute_btn_event_cb(lv_event_t * e) {
    (void)e;
    if (current_settings.is_muted) {
        lv_obj_clear_state(mfs_mute_btn, LV_STATE_CHECKED);
        lv_label_set_text(mfs_mute_label, LV_SYMBOL_VOLUME_MAX);
        current_settings.is_muted = false;
        toggle_mute(false);
    } else {
        lv_obj_add_state(mfs_mute_btn, LV_STATE_CHECKED);
        lv_label_set_text(mfs_mute_label, LV_SYMBOL_MUTE);
        current_settings.is_muted = true;
        toggle_mute(true);
    }
    mark_settings_dirty();
}

static void update_cc_btn_color(lv_obj_t * btn, uint16_t cc) {
    lv_color_t color;
    if (cc <= 200) color = lv_color_hex(0x00FFFF); // Cyan
    else if (cc <= 400) color = lv_palette_main(LV_PALETTE_BLUE); // Default Blue
    else if (cc <= 800) color = lv_palette_main(LV_PALETTE_PURPLE); // Purple
    else if (cc <= 1600) color = lv_palette_main(LV_PALETTE_ORANGE); // Orange
    else color = lv_palette_main(LV_PALETTE_RED); // Red

    lv_obj_set_style_bg_color(btn, color, 0);
}

static void cycle_count_event_cb(lv_event_t * e) {
    uint16_t c = current_settings.cycle_count;
    // Map existing invalid values safely, and set rotation
    if (c < 200) c = 200;
    else if (c == 200) c = 400;
    else if (c == 400) c = 800;
    else if (c == 800) c = 1600;
    else if (c == 1600) c = 3200;
    else c = 200;

    current_settings.cycle_count = c;
    if (mfs_cc_label) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%u", (unsigned int)c);
        lv_label_set_text(mfs_cc_label, buf);
    }
    update_cc_btn_color((lv_obj_t *)lv_event_get_target(e), c);
    mark_settings_dirty();
    set_rm3100_cycle_count(c);
}

static void log_btn_event_cb(lv_event_t * e) {
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t * label = (lv_obj_t *)lv_obj_get_child(btn, 0);
    if (lv_obj_has_state(btn, LV_STATE_CHECKED)) {
        start_logging();
        lv_label_set_text(label, "Stop Log");
    } else {
        stop_logging();
        lv_label_set_text(label, "Start Log");
    }
}

static void brightness_slider_cb(lv_event_t * e) {
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    int percent = lv_slider_get_value(slider);

    int duty = 255 - (percent * 255 / 100);
    if (duty > 240) duty = 240;
    if (duty < 0) duty = 0;

    set_backlight_pwm((uint8_t)duty);

    current_settings.brightness = percent;
    mark_settings_dirty();
}

static void wifi_btn_event_cb(lv_event_t * e) {
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t * label = (lv_obj_t *)lv_obj_get_child(btn, 0);
    if (lv_obj_has_state(btn, LV_STATE_CHECKED)) {
        lv_label_set_text(label, "Stop Wi-Fi");
        fic_web_portal_start();
    } else {
        lv_label_set_text(label, "Start Wi-Fi");
        fic_web_portal_stop();
    }
}

static void cal_msg_timer_cb(lv_timer_t * timer) {
    if (cal_status_label) {
        lv_label_set_text(cal_status_label, "Ready for Calibration");
        lv_obj_set_style_text_color(cal_status_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    }
    if (cal_progress_bar) {
        lv_bar_set_value(cal_progress_bar, 0, LV_ANIM_OFF);
    }
    cal_msg_timer = NULL;
    lv_timer_del(timer);
}

void show_calibration_result_msg(bool success) {
    if (mfs_lvgl_lock(-1)) {
        mfs_is_calibrating = false;
        if (full_cal_btn && full_cal_label) {
            lv_obj_set_style_bg_color(full_cal_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
            lv_label_set_text(full_cal_label, "Calibrate");
        }
        if (cal_status_label) {
            if (success) {
                lv_label_set_text(cal_status_label, "Calibration Complete!");
                lv_obj_set_style_text_color(cal_status_label, lv_color_hex(0x00ff00), LV_PART_MAIN);
            } else {
                lv_label_set_text(cal_status_label, "Math Solver Failed!");
                lv_obj_set_style_text_color(cal_status_label, lv_color_hex(0xff0000), LV_PART_MAIN);
            }
        }

        if (cal_msg_timer) lv_timer_del(cal_msg_timer);
        cal_msg_timer = lv_timer_create(cal_msg_timer_cb, 4000, NULL);

        mfs_lvgl_unlock();
    }
}

static void main_tare_btn_event_cb(lv_event_t * e) {
    (void)e;
    if (main_tare_state == 0) {
        // RAW -> TARE
        trigger_manual_tare();
        main_tare_state = 1;
        lv_label_set_text(mfs_tare_label, "TARE");
        lv_obj_set_style_bg_color(mfs_tare_btn, lv_palette_main(LV_PALETTE_RED), 0);
    } else if (main_tare_state == 1) {
        // TARE -> AUTO
        current_settings.auto_tare_on = true;
        set_auto_tare(true);
        mark_settings_dirty();
        main_tare_state = 2;
        lv_label_set_text(mfs_tare_label, "AUTO");
        lv_obj_set_style_bg_color(mfs_tare_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else {
        // AUTO -> RAW
        current_settings.auto_tare_on = false;
        set_auto_tare(false);
        mark_settings_dirty();
        reset_tare();
        main_tare_state = 0;
        lv_label_set_text(mfs_tare_label, "RAW");
        lv_obj_set_style_bg_color(mfs_tare_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    }
}

static void start_cal_event_cb(lv_event_t * e) {
    (void)e;
    if (!mfs_is_calibrating) {
        start_on_wand_calibration();
        mfs_is_calibrating = true;
        if (full_cal_btn && full_cal_label) {
            lv_obj_set_style_bg_color(full_cal_btn, lv_palette_main(LV_PALETTE_RED), 0);
            lv_label_set_text(full_cal_label, "STOP");
        }
        if (cal_status_label) lv_label_set_text(cal_status_label, "Capturing Data: 0%");
        if (cal_progress_bar) lv_bar_set_value(cal_progress_bar, 0, LV_ANIM_OFF);
    } else {
        stop_on_wand_calibration();
        mfs_is_calibrating = false;
        if (full_cal_btn && full_cal_label) {
            lv_obj_set_style_bg_color(full_cal_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
            lv_label_set_text(full_cal_label, "Calibrate");
        }
        if (cal_status_label) {
            lv_label_set_text(cal_status_label, "Stopped");
            lv_obj_set_style_text_color(cal_status_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        }

        if (cal_msg_timer) lv_timer_del(cal_msg_timer);
        cal_msg_timer = lv_timer_create(cal_msg_timer_cb, 3000, NULL);
    }
}

void create_detector_ui(void) {
    lv_obj_t * tv = lv_tileview_create(lv_screen_active());
    main_tv = tv;
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    // =====================================
    // Tile 1: Main Detector HUD
    // =====================================
    lv_obj_t * tile1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_set_scrollbar_mode(tile1, LV_SCROLLBAR_MODE_OFF);

    mfs_title_label = lv_label_create(tile1);
    lv_label_set_text(mfs_title_label, "Magnetic Field\nScanner");
    lv_obj_set_style_text_color(mfs_title_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(mfs_title_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(mfs_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(mfs_title_label, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * fw_label = lv_label_create(tile1);
    lv_label_set_text(fw_label, "FW: " MFS_FIRMWARE_VERSION);
    lv_obj_set_style_text_color(fw_label, lv_color_hex(0xaaaaaa), LV_PART_MAIN);
    lv_obj_set_style_text_font(fw_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(fw_label, LV_ALIGN_TOP_MID, 0, 60);

    // Background Arc (Left Half - Negative/South)
    mfs_bg_arc_left = lv_arc_create(tile1);
    lv_obj_set_size(mfs_bg_arc_left, 160, 160);
    lv_arc_set_rotation(mfs_bg_arc_left, 0);
    lv_arc_set_bg_angles(mfs_bg_arc_left, 90, 225); // Left side only
    lv_obj_align(mfs_bg_arc_left, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_style_arc_color(mfs_bg_arc_left, lv_color_hex(0x002266), LV_PART_MAIN);
    lv_obj_remove_style(mfs_bg_arc_left, NULL, LV_PART_KNOB);
    lv_obj_remove_style(mfs_bg_arc_left, NULL, LV_PART_INDICATOR);
    lv_obj_set_clickable(mfs_bg_arc_left, false);

    // Background Arc (Right Half - Positive/North)
    mfs_bg_arc_right = lv_arc_create(tile1);
    lv_obj_set_size(mfs_bg_arc_right, 160, 160);
    lv_arc_set_rotation(mfs_bg_arc_right, 0);
    lv_arc_set_bg_angles(mfs_bg_arc_right, 315, 90); // Right side only
    lv_obj_align(mfs_bg_arc_right, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_style_arc_color(mfs_bg_arc_right, lv_color_hex(0x660000), LV_PART_MAIN);
    lv_obj_remove_style(mfs_bg_arc_right, NULL, LV_PART_KNOB);
    lv_obj_remove_style(mfs_bg_arc_right, NULL, LV_PART_INDICATOR);
    lv_obj_set_clickable(mfs_bg_arc_right, false);

    // Main Active Arc
    mfs_mag_arc = lv_arc_create(tile1);
    lv_obj_set_size(mfs_mag_arc, 160, 160);
    lv_arc_set_rotation(mfs_mag_arc, 0);
    lv_arc_set_bg_angles(mfs_mag_arc, 315, 225);
    lv_arc_set_mode(mfs_mag_arc, LV_ARC_MODE_SYMMETRICAL);
    lv_obj_set_style_arc_opa(mfs_mag_arc, 0, LV_PART_MAIN);
    lv_arc_set_range(mfs_mag_arc, -5000, 5000);
    lv_arc_set_value(mfs_mag_arc, 0);
    lv_obj_align(mfs_mag_arc, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_remove_style(mfs_mag_arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(mfs_mag_arc, false);

    // Radar Grid Lines
    lv_obj_t * h_line = lv_obj_create(mfs_mag_arc);
    lv_obj_set_size(h_line, 140, 2);
    lv_obj_set_style_bg_color(h_line, lv_color_hex(0x004400), LV_PART_MAIN);
    lv_obj_set_style_border_width(h_line, 0, LV_PART_MAIN);
    lv_obj_center(h_line);

    lv_obj_t * v_line = lv_obj_create(mfs_mag_arc);
    lv_obj_set_size(v_line, 2, 140);
    lv_obj_set_style_bg_color(v_line, lv_color_hex(0x004400), LV_PART_MAIN);
    lv_obj_set_style_border_width(v_line, 0, LV_PART_MAIN);
    lv_obj_center(v_line);

    // Crosshair Dot
    mfs_crosshair_dot = lv_obj_create(mfs_mag_arc);
    lv_obj_set_size(mfs_crosshair_dot, 10, 10);
    lv_obj_set_style_radius(mfs_crosshair_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(mfs_crosshair_dot, lv_color_hex(0x00ff00), LV_PART_MAIN);
    lv_obj_set_style_border_width(mfs_crosshair_dot, 0, LV_PART_MAIN);
    lv_obj_center(mfs_crosshair_dot);

    // Compass Labels (Minimap)
    mfs_compass_label_n = lv_label_create(mfs_mag_arc);
    lv_label_set_text(mfs_compass_label_n, "N");
    lv_obj_set_style_text_color(mfs_compass_label_n, lv_color_hex(0xff0000), 0);

    mfs_compass_label_e = lv_label_create(mfs_mag_arc);
    lv_label_set_text(mfs_compass_label_e, "E");
    lv_obj_set_style_text_color(mfs_compass_label_e, lv_color_hex(0xffffff), 0);

    mfs_compass_label_s = lv_label_create(mfs_mag_arc);
    lv_label_set_text(mfs_compass_label_s, "S");
    lv_obj_set_style_text_color(mfs_compass_label_s, lv_color_hex(0xffffff), 0);

    mfs_compass_label_w = lv_label_create(mfs_mag_arc);
    lv_label_set_text(mfs_compass_label_w, "W");
    lv_obj_set_style_text_color(mfs_compass_label_w, lv_color_hex(0xffffff), 0);

    // Intercardinal Ticks
    mfs_compass_label_ne = lv_label_create(mfs_mag_arc);
    lv_label_set_text(mfs_compass_label_ne, "NE");
    lv_obj_set_style_text_color(mfs_compass_label_ne, lv_color_hex(0x888888), 0);

    mfs_compass_label_se = lv_label_create(mfs_mag_arc);
    lv_label_set_text(mfs_compass_label_se, "SE");
    lv_obj_set_style_text_color(mfs_compass_label_se, lv_color_hex(0x888888), 0);

    mfs_compass_label_sw = lv_label_create(mfs_mag_arc);
    lv_label_set_text(mfs_compass_label_sw, "SW");
    lv_obj_set_style_text_color(mfs_compass_label_sw, lv_color_hex(0x888888), 0);

    mfs_compass_label_nw = lv_label_create(mfs_mag_arc);
    lv_label_set_text(mfs_compass_label_nw, "NW");
    lv_obj_set_style_text_color(mfs_compass_label_nw, lv_color_hex(0x888888), 0);

    mfs_declination_label = lv_label_create(tile1);
    lv_obj_set_style_text_color(mfs_declination_label, lv_color_hex(0x888888), 0);
    lv_obj_align(mfs_declination_label, LV_ALIGN_TOP_RIGHT, -5, 110);

    mfs_elevation_label = lv_label_create(tile1);
    lv_obj_set_style_text_color(mfs_elevation_label, lv_color_hex(0xffaa00), 0);
    lv_obj_align(mfs_elevation_label, LV_ALIGN_TOP_LEFT, 5, 110);

    // Huge Mag Label
    mfs_mag_label = lv_label_create(tile1);
    lv_obj_set_width(mfs_mag_label, 172);
    lv_obj_set_style_text_align(mfs_mag_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(mfs_mag_label, "0");
    lv_obj_set_style_text_color(mfs_mag_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(mfs_mag_label, &lv_font_montserrat_36, LV_PART_MAIN);
    lv_obj_align(mfs_mag_label, LV_ALIGN_TOP_MID, 0, 280);

    // Medium nT Label
    mfs_nt_label = lv_label_create(tile1);
    lv_obj_set_width(mfs_nt_label, 172);
    lv_obj_set_style_text_align(mfs_nt_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(mfs_nt_label, "0.0 nT");
    lv_obj_set_style_text_color(mfs_nt_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(mfs_nt_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(mfs_nt_label, LV_ALIGN_TOP_MID, 0, 340);

    // Polarity Label
    mfs_polarity_label = lv_label_create(tile1);
    lv_obj_set_width(mfs_polarity_label, 172);
    lv_obj_set_style_text_align(mfs_polarity_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(mfs_polarity_label, " ");
    lv_obj_set_style_text_color(mfs_polarity_label, lv_color_hex(0xffa500), LV_PART_MAIN);
    lv_obj_set_style_text_font(mfs_polarity_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(mfs_polarity_label, LV_ALIGN_TOP_MID, 0, 380);

    mfs_mute_btn = lv_button_create(tile1);
    lv_obj_set_size(mfs_mute_btn, 70, 50);
    lv_obj_align(mfs_mute_btn, LV_ALIGN_TOP_MID, -40, 420);
    lv_obj_add_event_cb(mfs_mute_btn, mute_btn_event_cb, LV_EVENT_CLICKED, NULL);
    mfs_mute_label = lv_label_create(mfs_mute_btn);
    if (current_settings.is_muted) {
        lv_obj_add_state(mfs_mute_btn, LV_STATE_CHECKED);
        lv_label_set_text(mfs_mute_label, LV_SYMBOL_MUTE);
    } else {
        lv_label_set_text(mfs_mute_label, LV_SYMBOL_VOLUME_MAX);
    }
    lv_obj_center(mfs_mute_label);

    mfs_wave_btn = lv_button_create(tile1);
    lv_obj_set_size(mfs_wave_btn, 70, 50);
    lv_obj_align(mfs_wave_btn, LV_ALIGN_TOP_MID, 40, 420);
    lv_obj_add_event_cb(mfs_wave_btn, wave_btn_event_cb, LV_EVENT_CLICKED, NULL);
    mfs_wave_label = lv_label_create(mfs_wave_btn);
    if (current_settings.audio_waveform == 0) lv_label_set_text(mfs_wave_label, "SQR");
    else if (current_settings.audio_waveform == 1) lv_label_set_text(mfs_wave_label, "TRI");
    else if (current_settings.audio_waveform == 2) lv_label_set_text(mfs_wave_label, "SIN");
    else lv_label_set_text(mfs_wave_label, "GCM");
    lv_obj_center(mfs_wave_label);

    mfs_scan_btn = lv_button_create(tile1);
    lv_obj_set_size(mfs_scan_btn, 70, 50);
    lv_obj_align(mfs_scan_btn, LV_ALIGN_TOP_MID, -40, 490);
    lv_obj_add_event_cb(mfs_scan_btn, scan_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_state(mfs_scan_btn, LV_STATE_CHECKED);
    mfs_scan_label = lv_label_create(mfs_scan_btn);
    lv_label_set_text(mfs_scan_label, LV_SYMBOL_PLAY);
    lv_obj_center(mfs_scan_label);

    mfs_tare_btn = lv_button_create(tile1);
    lv_obj_set_size(mfs_tare_btn, 70, 50);
    lv_obj_align(mfs_tare_btn, LV_ALIGN_TOP_MID, 40, 490);
    lv_obj_set_style_bg_color(mfs_tare_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(mfs_tare_btn, main_tare_btn_event_cb, LV_EVENT_CLICKED, NULL);
    mfs_tare_label = lv_label_create(mfs_tare_btn);
    lv_label_set_text(mfs_tare_label, "RAW");
    lv_obj_center(mfs_tare_label);

    // Date/Time Labels
    ui_date_label = lv_label_create(tile1);
    lv_obj_align(ui_date_label, LV_ALIGN_TOP_MID, 0, 560);
    lv_obj_set_style_text_color(ui_date_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(ui_date_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(ui_date_label, "00/00/0000");

    ui_time_label = lv_label_create(tile1);
    lv_obj_align(ui_time_label, LV_ALIGN_TOP_MID, 0, 590);
    lv_obj_set_style_text_color(ui_time_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(ui_time_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(ui_time_label, "00:00:00");

    // =====================================
    // Tile 2: Calibration & Tracking
    // =====================================
    lv_obj_t * tile2 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT);
    lv_obj_set_scrollbar_mode(tile2, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * cal_title = lv_label_create(tile2);
    lv_label_set_text(cal_title, "Calibration");
    lv_obj_set_style_text_align(cal_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(cal_title, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(cal_title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(cal_title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * fw_label2 = lv_label_create(tile2);
    lv_label_set_text(fw_label2, "FW: " MFS_FIRMWARE_VERSION);
    lv_obj_set_style_text_color(fw_label2, lv_color_hex(0xaaaaaa), LV_PART_MAIN);
    lv_obj_set_style_text_font(fw_label2, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(fw_label2, LV_ALIGN_TOP_MID, 0, 60);

    cal_status_label = lv_label_create(tile2);
    lv_label_set_long_mode(cal_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cal_status_label, 160);
    lv_obj_set_style_text_align(cal_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(cal_status_label, "Ready for Calibration");
    lv_obj_set_style_text_color(cal_status_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(cal_status_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(cal_status_label, LV_ALIGN_TOP_MID, 0, 140);

    cal_progress_bar = lv_bar_create(tile2);
    lv_obj_set_size(cal_progress_bar, 140, 20);
    lv_obj_align(cal_progress_bar, LV_ALIGN_TOP_MID, 0, 215);
    lv_bar_set_range(cal_progress_bar, 0, 100);
    lv_bar_set_value(cal_progress_bar, 0, LV_ANIM_OFF);

    full_cal_btn = lv_button_create(tile2);
    lv_obj_set_size(full_cal_btn, 140, 50);
    lv_obj_align(full_cal_btn, LV_ALIGN_TOP_MID, 0, 250);
    lv_obj_set_style_bg_color(full_cal_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    full_cal_label = lv_label_create(full_cal_btn);
    lv_label_set_text(full_cal_label, "Calibrate");
    lv_obj_add_event_cb(full_cal_btn, start_cal_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_text_align(full_cal_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(full_cal_label);

    // =====================================
    // Tile 3: System, Hardware & Data
    // =====================================
    lv_obj_t * tile3 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_set_scrollbar_mode(tile3, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * sys_title = lv_label_create(tile3);
    mfs_batt_label = lv_label_create(tile3);
    lv_label_set_text(mfs_batt_label, "Bat: --.--V");
    lv_obj_set_style_text_color(mfs_batt_label, lv_color_hex(0xffaa00), LV_PART_MAIN);
    lv_obj_set_style_text_font(mfs_batt_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(mfs_batt_label, LV_ALIGN_TOP_MID, 0, 90);

    lv_label_set_text(sys_title, "System\n& Hardware");
    lv_obj_set_style_text_align(sys_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(sys_title, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(sys_title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(sys_title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * fw_label3 = lv_label_create(tile3);
    lv_label_set_text(fw_label3, "FW: " MFS_FIRMWARE_VERSION);
    lv_obj_set_style_text_color(fw_label3, lv_color_hex(0xaaaaaa), LV_PART_MAIN);
    lv_obj_set_style_text_font(fw_label3, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(fw_label3, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t * bright_label = lv_label_create(tile3);
    lv_label_set_text(bright_label, "Brightness");
    lv_obj_set_style_text_color(bright_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(bright_label, LV_ALIGN_TOP_MID, 0, 140);

    lv_obj_t * brightness_slider = lv_slider_create(tile3);
    lv_obj_set_width(brightness_slider, 140);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, 170);
    lv_slider_set_range(brightness_slider, 1, 100);
    lv_slider_set_value(brightness_slider, current_settings.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * vol_label = lv_label_create(tile3);
    lv_label_set_text(vol_label, "Volume");
    lv_obj_set_style_text_color(vol_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(vol_label, LV_ALIGN_TOP_MID, 0, 210);

    lv_obj_t * vol_slider = lv_slider_create(tile3);
    lv_obj_set_width(vol_slider, 140);
    lv_obj_align(vol_slider, LV_ALIGN_TOP_MID, 0, 240);
    lv_slider_set_range(vol_slider, 0, 100);
    lv_slider_set_value(vol_slider, current_settings.volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(vol_slider, vol_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * gain_label = lv_label_create(tile3);
    lv_label_set_text(gain_label, "Audio Gain");
    lv_obj_set_style_text_color(gain_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(gain_label, LV_ALIGN_TOP_MID, 0, 280);

    lv_obj_t * gain_slider = lv_slider_create(tile3);
    lv_obj_set_width(gain_slider, 140);
    lv_obj_align(gain_slider, LV_ALIGN_TOP_MID, 0, 310);
    lv_slider_set_range(gain_slider, 1, 100);
    lv_slider_set_value(gain_slider, current_settings.audio_gain, LV_ANIM_OFF);
    lv_obj_add_event_cb(gain_slider, gain_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * roller_label = lv_label_create(tile3);
    lv_label_set_text(roller_label, "Cycle Counts");
    lv_obj_set_style_text_color(roller_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(roller_label, LV_ALIGN_TOP_MID, 0, 360);

    lv_obj_t * cc_btn = lv_button_create(tile3);
    lv_obj_set_size(cc_btn, 140, 40);
    lv_obj_align(cc_btn, LV_ALIGN_TOP_MID, 0, 390);
    lv_obj_add_event_cb(cc_btn, cycle_count_event_cb, LV_EVENT_CLICKED, NULL);
    mfs_cc_label = lv_label_create(cc_btn);
    {
        char cc_buf[12];
        snprintf(cc_buf, sizeof(cc_buf), "%u", (unsigned int)current_settings.cycle_count);
        lv_label_set_text(mfs_cc_label, cc_buf);
    }
    lv_obj_center(mfs_cc_label);
    update_cc_btn_color(cc_btn, current_settings.cycle_count);

    lv_obj_t * log_btn = lv_button_create(tile3);
    lv_obj_set_size(log_btn, 140, 40);
    lv_obj_align(log_btn, LV_ALIGN_TOP_MID, 0, 465);
    lv_obj_set_checkable(log_btn, true);
    lv_obj_add_event_cb(log_btn, log_btn_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t * log_label = lv_label_create(log_btn);
    lv_label_set_text(log_label, "Start Log");
    lv_obj_center(log_label);

    mfs_wifi_btn = lv_button_create(tile3);
    lv_obj_set_size(mfs_wifi_btn, 140, 40);
    lv_obj_align(mfs_wifi_btn, LV_ALIGN_TOP_MID, 0, 525);
    lv_obj_set_checkable(mfs_wifi_btn, true);
    lv_obj_add_event_cb(mfs_wifi_btn, wifi_btn_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    mfs_wifi_label = lv_label_create(mfs_wifi_btn);
    if (fic_web_portal_is_active()) {
        lv_obj_add_state(mfs_wifi_btn, LV_STATE_CHECKED);
        lv_label_set_text(mfs_wifi_label, "Stop Wi-Fi");
    } else {
        lv_label_set_text(mfs_wifi_label, "Start Wi-Fi");
    }
    lv_obj_center(mfs_wifi_label);

    mfs_ip_label = lv_label_create(tile3);
    lv_label_set_text(mfs_ip_label, "IP: Disconnected");
    lv_obj_set_style_text_color(mfs_ip_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(mfs_ip_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(mfs_ip_label, LV_ALIGN_TOP_MID, 0, 585);

    lv_timer_create(wifi_status_poll_timer_cb, 250, NULL);

    // Apply loaded settings to hardware on boot
    int duty = 255 - (current_settings.brightness * 255 / 100);
    if (duty > 240) duty = 240;
    if (duty < 0) duty = 0;
    set_backlight_pwm((uint8_t)duty);

    toggle_mute(current_settings.is_muted);
    set_audio_volume(current_settings.volume);
    set_audio_gain(current_settings.audio_gain);
    set_rm3100_cycle_count(current_settings.cycle_count);
}
