#include "screenshot.h"
#include "../../user_config.h"
#include <SD.h>
#include <FFat.h>
#include "../settings_manager/settings_manager.h"
#include "../data_logger/data_logger.h"
#include "../lvgl_port/lvgl_port.h"
#include <lvgl.h>
extern lv_obj_t * main_tv;

extern "C" void take_screenshot_to_sd(void) {
    if (!sd_card_mounted) return;
    
    char filename[64];
    char ts[32];
    get_formatted_timestamp(ts, sizeof(ts), false);
    snprintf(filename, sizeof(filename), "/screenshot_%s.bmp", ts);
    
    // 1. Acquire LVGL Mutex so UI doesn't draw while we snap
    if (!mfs_lvgl_lock(5000)) {
        Serial.println("Failed to lock LVGL for screenshot");
        return;
    }
    
    // 2. Take Snapshot of the active tile (Allocates ~220KB in PSRAM)
    lv_obj_t * target_obj = lv_screen_active();
    if (main_tv) {
        target_obj = lv_tileview_get_tile_active(main_tv);
        if (!target_obj) target_obj = lv_screen_active();
    }
    
    lv_draw_buf_t * snap = lv_snapshot_take(target_obj, LV_COLOR_FORMAT_RGB565);
    
    if (snap == NULL) {
        Serial.println("Screenshot alloc failed");
        mfs_lvgl_unlock();
        return;
    }
    
    // 3. Write 24-bit BMP to SD Card (Zero dependencies, natively opens on PC)
    File file = SD.open(filename, FILE_WRITE);
    if (file) {
        uint32_t w = snap->header.w;
        uint32_t h = snap->header.h;
        uint32_t row_size = ((w * 3 + 3) & ~3); // pad to 4 bytes
        uint32_t image_size = row_size * h;
        uint32_t file_size = 54 + image_size;
        
        uint8_t header[54] = {
            'B','M', 
            (uint8_t)(file_size), (uint8_t)(file_size>>8), (uint8_t)(file_size>>16), (uint8_t)(file_size>>24),
            0,0,0,0, 54,0,0,0, 40,0,0,0,
            (uint8_t)(w), (uint8_t)(w>>8), (uint8_t)(w>>16), (uint8_t)(w>>24),
            (uint8_t)(-h), (uint8_t)((-h)>>8), (uint8_t)((-h)>>16), (uint8_t)((-h)>>24), // Negative height = top-down
            1,0, 24,0, // 24 bits per pixel
            0,0,0,0,
            (uint8_t)(image_size), (uint8_t)(image_size>>8), (uint8_t)(image_size>>16), (uint8_t)(image_size>>24),
            0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
        };
        file.write(header, 54);
        
        uint8_t * p565 = (uint8_t*)snap->data;
        uint8_t row24[w * 3 + 4]; // ~520 bytes max buffer
        
        for (int y = 0; y < h; y++) {
            int i24 = 0;
            for (int x = 0; x < w; x++) {
                uint16_t color = (p565[1] << 8) | p565[0];
                p565 += 2;
                
                // RGB565 to RGB888 (BGR order for BMP)
                uint8_t r = (color >> 11) & 0x1F;
                uint8_t g = (color >> 5) & 0x3F;
                uint8_t b = color & 0x1F;
                
                row24[i24++] = (b * 255) / 31;
                row24[i24++] = (g * 255) / 63;
                row24[i24++] = (r * 255) / 31;
            }
            while (i24 < row_size) row24[i24++] = 0; // padding
            file.write(row24, row_size);
        }
        file.close();
        Serial.printf("Saved screenshot to %s\n", filename);
    }
    
    // 4. Free PSRAM and Unlock
    lv_draw_buf_destroy(snap);
    mfs_lvgl_unlock();
}
