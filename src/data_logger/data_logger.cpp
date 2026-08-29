#include "data_logger.h"
#include "../../user_config.h"
#include "../settings_manager/settings_manager.h"
#include <SD.h>
#include <FFat.h>
fs::FS& get_active_fs();
static const uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static bool isLeapYear(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}
#include "../PCF85063/rtc_bsp.h"
#include "../matrix_math/matrix_math.h"
volatile bool is_logging = false; uint32_t log_sample_count = 0; String current_log_filename = ""; File logFile; SemaphoreHandle_t log_mux = NULL;
extern "C" void get_formatted_timestamp(char* buffer, size_t max_len, bool include_ms) {
    RtcDateTime_t t = i2c_rtc_get();
    
    // Apply offset manually
    int total_minutes = t.hour * 60 + t.minute + current_settings.minutesOffsetToUTC;
    
    int new_hour = t.hour;
    int new_min = t.minute;
    int day_offset = 0;
    
    if (total_minutes < 0) {
        int days_back = (abs(total_minutes) + 1439) / 1440; // ceil division
        day_offset = -days_back;
        total_minutes = (total_minutes % 1440) + 1440; // positive modulo
    } else if (total_minutes >= 1440) {
        day_offset = total_minutes / 1440;
        total_minutes = total_minutes % 1440;
    }
    
    new_hour = total_minutes / 60;
    new_min = total_minutes % 60;
    
    int new_year = t.year;
    int new_month = t.month;
    int new_day = t.day;
    
    if (day_offset != 0) {
        new_day += day_offset;
        while (new_day < 1) {
            new_month--;
            if (new_month < 1) {
                new_month = 12;
                new_year--;
            }
            uint8_t dim = daysInMonth[new_month - 1];
            if (new_month == 2 && isLeapYear(new_year)) dim = 29;
            new_day += dim;
        }
        while (true) {
            uint8_t dim = daysInMonth[new_month - 1];
            if (new_month == 2 && isLeapYear(new_year)) dim = 29;
            if (new_day <= dim) break;
            
            new_day -= dim;
            new_month++;
            if (new_month > 12) {
                new_month = 1;
                new_year++;
            }
        }
    }
    
    uint16_t ms = (millis() % 1000);
    
    char sign = (current_settings.minutesOffsetToUTC >= 0) ? '+' : '-';
    int abs_offset = abs(current_settings.minutesOffsetToUTC);
    
    if (include_ms) {
        snprintf(buffer, max_len, "%04d-%02d-%02d_%02d-%02d-%02d.%03d%c%d", 
                 new_year, new_month, new_day, new_hour, new_min, t.second, ms, sign, abs_offset);
    } else {
        snprintf(buffer, max_len, "%04d-%02d-%02d_%02d-%02d-%02d%c%d", 
                 new_year, new_month, new_day, new_hour, new_min, t.second, sign, abs_offset);
    }
}


extern "C" void start_logging(void) {
    if (log_mux) xSemaphoreTake(log_mux, portMAX_DELAY);
    if (logFile) logFile.close();
    
    char ts[64];
    get_formatted_timestamp(ts, sizeof(ts), false);
    current_log_filename = "/log_" + String(ts) + ".csv";
    
    logFile = get_active_fs().open(current_log_filename, "w");
    if (!logFile) {
        Serial.println("Failed to open log file for writing");
        if (log_mux) xSemaphoreGive(log_mux);
        return;
    }
    
    logFile.println("time_ms,timestamp,version,voltage,audio_gain,cc,refX_raw,refY_raw,refZ_raw,tipX_raw,tipY_raw,tipZ_raw,refX_cal,refY_cal,refZ_cal,tipX_cal,tipY_cal,tipZ_cal,calOffsetX,calOffsetY,calOffsetZ,gradX,gradY,gradZ,mag,nT,accX,accY,accZ,gyrX,gyrY,gyrZ,freq,is_muted,QW,QX,QY,QZ,Azimuth,Elevation,Declination");
    log_sample_count = 0;
    is_logging = true;
    if (log_mux) xSemaphoreGive(log_mux);
}

extern "C" void start_calibration_logging(void) {
    if (log_mux) xSemaphoreTake(log_mux, portMAX_DELAY);
    if (logFile) logFile.close();
    
    char ts[64];
    get_formatted_timestamp(ts, sizeof(ts), false);
    current_log_filename = "/calibration_" + String(ts) + ".csv";
    
    logFile = get_active_fs().open(current_log_filename, "w");
    if (!logFile) {
        Serial.println("Failed to open calibration file for writing");
        if (log_mux) xSemaphoreGive(log_mux);
        return;
    }
    
    logFile.println("time_ms,timestamp,version,voltage,audio_gain,cc,refX_raw,refY_raw,refZ_raw,tipX_raw,tipY_raw,tipZ_raw,refX_cal,refY_cal,refZ_cal,tipX_cal,tipY_cal,tipZ_cal,calOffsetX,calOffsetY,calOffsetZ,gradX,gradY,gradZ,mag,nT,accX,accY,accZ,gyrX,gyrY,gyrZ,freq,is_muted,QW,QX,QY,QZ,Azimuth,Elevation,Declination");
    log_sample_count = 0;
    is_logging = true;
    if (log_mux) xSemaphoreGive(log_mux);
}

extern "C" void stop_logging(void) {
    if (log_mux) xSemaphoreTake(log_mux, portMAX_DELAY);
    is_logging = false;
    if (logFile) {
        logFile.close();
    }
    if (log_mux) xSemaphoreGive(log_mux);
}


#include "../matrix_math/matrix_math.h"

extern "C" bool process_calibration_file(void) {
    if (current_log_filename.length() == 0) return false;
    
    File file = get_active_fs().open(current_log_filename, "r");
    if (!file) {
        Serial.println("process_calibration_file: Failed to open calibration CSV");
        return false;
    }
    
    // Allocate temporary PSRAM buffers
    int32_t *cal_ref_x = (int32_t *)heap_caps_malloc(CALIBRATION_POINTS * sizeof(int32_t), MALLOC_CAP_SPIRAM);
    int32_t *cal_ref_y = (int32_t *)heap_caps_malloc(CALIBRATION_POINTS * sizeof(int32_t), MALLOC_CAP_SPIRAM);
    int32_t *cal_ref_z = (int32_t *)heap_caps_malloc(CALIBRATION_POINTS * sizeof(int32_t), MALLOC_CAP_SPIRAM);
    int32_t *cal_tip_x = (int32_t *)heap_caps_malloc(CALIBRATION_POINTS * sizeof(int32_t), MALLOC_CAP_SPIRAM);
    int32_t *cal_tip_y = (int32_t *)heap_caps_malloc(CALIBRATION_POINTS * sizeof(int32_t), MALLOC_CAP_SPIRAM);
    int32_t *cal_tip_z = (int32_t *)heap_caps_malloc(CALIBRATION_POINTS * sizeof(int32_t), MALLOC_CAP_SPIRAM);
    
    if (!cal_ref_x || !cal_tip_x) {
        Serial.println("process_calibration_file: Failed to allocate PSRAM buffers");
        if (cal_ref_x) free(cal_ref_x); if (cal_ref_y) free(cal_ref_y); if (cal_ref_z) free(cal_ref_z);
        if (cal_tip_x) free(cal_tip_x); if (cal_tip_y) free(cal_tip_y); if (cal_tip_z) free(cal_tip_z);
        file.close();
        return false;
    }
    
    int count = 0;
    bool is_first_line = true;
    
    while (file.available() && count < CALIBRATION_POINTS) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        
        if (is_first_line) {
            is_first_line = false;
            continue; // Skip header
        }
        
        // Parse 33 columns. refX is idx 6, tipZ is idx 11
        // 0:time_ms, 1:timestamp, 2:version, 3:voltage, 4:audio_gain, 5:cc,
        // 6:refX, 7:refY, 8:refZ, 9:tipX, 10:tipY, 11:tipZ
        int col = 0;
        int start_idx = 0;
        int len = line.length();
        int32_t parsed_vals[12] = {0}; // We need up to index 11
        
        for (int i = 0; i <= len && col < 12; i++) {
            if (i == len || line.charAt(i) == ',') {
                if (col >= 6 && col <= 11) {
                    parsed_vals[col] = line.substring(start_idx, i).toInt();
                }
                start_idx = i + 1;
                col++;
            }
        }
        
        // Filter out absurd spikes (e.g. EMI glitch on long I2C wires)
        // Earth's magnetic field at CC=800 is roughly ~30,000 counts. 
        // Any value > 50,000 is physically impossible without a neodymium magnet.
        bool is_valid = true;
        for (int i=6; i<=11; i++) {
            if (abs(parsed_vals[i]) > 50000) {
                is_valid = false;
                break;
            }
        }
        
        if (is_valid) {
            cal_ref_x[count] = parsed_vals[6];
            cal_ref_y[count] = parsed_vals[7];
            cal_ref_z[count] = parsed_vals[8];
            cal_tip_x[count] = parsed_vals[9];
            cal_tip_y[count] = parsed_vals[10];
            cal_tip_z[count] = parsed_vals[11];
            count++;
            
            // Pet the watchdog to prevent a crash during this long parsing loop!
            if (count % 100 == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
    }
    file.close();
    
    bool success = false;
    if (count > 100) { // Ensure we have enough valid data points
        float ref_center[3], ref_soft[3][3];
        float tip_center[3], tip_soft[3][3];
        
        bool ref_ok = get_calibration_matrices(cal_ref_x, cal_ref_y, cal_ref_z, count, ref_center, ref_soft);
        bool tip_ok = get_calibration_matrices(cal_tip_x, cal_tip_y, cal_tip_z, count, tip_center, tip_soft);
        
        
        if (ref_ok && tip_ok) {
            // Apply Kabsch alignment to align Tip to Ref
            float R_align[3][3];
            
            if (kabsch_align_3x3(cal_ref_x, cal_ref_y, cal_ref_z, 
                                 cal_tip_x, cal_tip_y, cal_tip_z, 
                                 count, ref_center, ref_soft, tip_center, tip_soft, R_align)) {
                
                // Multiply Tip Soft-Iron Matrix by R_align
                // R_align * tip_soft
                float new_tip_soft[3][3];
                mat_mult_3x3_float(R_align, tip_soft, new_tip_soft);
                
                for(int i=0; i<3; i++) {
                    for(int j=0; j<3; j++) {
                        tip_soft[i][j] = new_tip_soft[i][j];
                    }
                }
                Serial.println("process_calibration_file: Kabsch physical alignment successfully applied.");
            }
            
            save_calibration(ref_center, ref_soft, tip_center, tip_soft);
            
            // Instantly apply to live memory
            for(int i=0; i<3; i++) {
                cal_config.ref_hard[i] = ref_center[i];
                cal_config.tip_hard[i] = tip_center[i];
                for(int j=0; j<3; j++) {
                    cal_config.ref_soft[i][j] = ref_soft[i][j];
                    cal_config.tip_soft[i][j] = tip_soft[i][j];
                }
            }
            success = true;
        } else {
            Serial.println("process_calibration_file: Math solver failed to converge.");
        }
    } else {
        Serial.println("process_calibration_file: Not enough valid data points found.");
    }
    
    // Free buffers
    free(cal_ref_x); free(cal_ref_y); free(cal_ref_z);
    free(cal_tip_x); free(cal_tip_y); free(cal_tip_z);
    
    return success;
}


extern "C" void log_data(uint32_t timestamp, float voltage, float audio_gain, int cc, int32_t refX_raw, int32_t refY_raw, int32_t refZ_raw, int32_t tipX_raw, int32_t tipY_raw, int32_t tipZ_raw, int32_t refX_cal, int32_t refY_cal, int32_t refZ_cal, int32_t tipX_cal, int32_t tipY_cal, int32_t tipZ_cal, int32_t calOffsetX, int32_t calOffsetY, int32_t calOffsetZ, int32_t gradX, int32_t gradY, int32_t gradZ, float mag, float nT, float accX, float accY, float accZ, float gyrX, float gyrY, float gyrZ, float freq, bool is_muted, float qw, float qx, float qy, float qz, float azimuth, float elevation, float declination) {
    char ts[64];
    get_formatted_timestamp(ts, sizeof(ts), true);
    
    char buffer[512];
    snprintf(buffer, sizeof(buffer), 
             "%lu,%s,%s,%.2f,%.1f,%d,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%.1f,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%d,%.4f,%.4f,%.4f,%.4f,%.1f,%.1f,%.1f",
             timestamp, ts, FIRMWARE_VERSION, voltage, audio_gain, cc,
             refX_raw, refY_raw, refZ_raw,
             tipX_raw, tipY_raw, tipZ_raw,
             refX_cal, refY_cal, refZ_cal,
             tipX_cal, tipY_cal, tipZ_cal,
             calOffsetX, calOffsetY, calOffsetZ,
             gradX, gradY, gradZ,
             mag, nT,
             accX, accY, accZ,
             gyrX, gyrY, gyrZ,
             freq, is_muted ? 1 : 0, qw, qx, qy, qz, azimuth, elevation, declination);

    if (current_settings.enable_serial_logging) {
        Serial.println(buffer);
    } else {
        if (log_sample_count < 10) {
            Serial.println(buffer);
        } else if (log_sample_count == 10) {
            Serial.println("--- Serial logging disabled by settings ---");
        }
    }

    if (log_mux) {
        if (xSemaphoreTake(log_mux, 0) == pdTRUE) {
            if (is_logging && logFile) {
                logFile.println(buffer);
            }
            xSemaphoreGive(log_mux);
        }
    }
    
    log_sample_count++;

}


