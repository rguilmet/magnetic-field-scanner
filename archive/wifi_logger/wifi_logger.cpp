#include "../../user_config.h"
#include "wifi_logger.h"

#include <SD.h>
#include <FFat.h>
#include "esp_heap_caps.h"

fs::FS& get_active_fs() {
    if (sd_card_mounted) {
        return SD;
    }
    return FFat;
}

#include <dirent.h>
#include <sys/stat.h>
#include "src/PCF85063/rtc_bsp.h"
#include <WiFi.h>
#include <WebServer.h>

#include <ArduinoJson.h>

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

CalibrationConfig cal_config = {
    {24.10f, -9.20f, -32.84f},
    {{0.9890f, 0.0214f, -0.1652f}, {-0.0385f, 1.0030f, 0.0006f}, {0.1710f, -0.0272f, 0.9793f}},
    {59.28f, 80.59f, -11.78f},
    {{1.0163f, -0.0080f, 0.0049f}, {-0.0080f, 0.9941f, 0.0048f}, {0.0049f, 0.0048f, 0.9902f}},
    60.0f
};


struct SystemSettings current_settings = {
    0,      // minutesOffsetToUTC
    50,     // brightness
    50,     // volume
    50,     // audio_gain
    200,    // cycle_count
    false,  // is_muted
    false,  // auto_tare_on
    40.0f,  // audio_base_freq
    2500.0f,// audio_max_freq
    0,      // audio_waveform (0=Square)
    "Your_SSID",      // wifi_ssid
    "Your_PASSWORD",  // wifi_password
    true,             // enable_serial_logging
    "UTC"             // timezone_label
};
volatile bool settings_dirty = false;
volatile uint32_t last_settings_change_ms = 0;
static volatile bool is_logging = false;
static uint32_t log_sample_count = 0;
bool wifi_active = false;
static String current_log_filename = "";
static File logFile;
static SemaphoreHandle_t log_mux = NULL;


extern "C" void load_settings(void) {
    File file = get_active_fs().open("/settings.json", "r");
    // Smart Fallback: If using SD but settings don't exist yet, try to load from FFat
    if (!file && sd_card_mounted) {
        file = FFat.open("/settings.json", "r");
        if (file) Serial.println("Loaded settings.json from FFat fallback (will save to SD later)");
    }
    
    if (!file) {
        Serial.println("No settings.json found, using defaults.");
        return;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    if (!error) {
        if (!doc["minutesOffsetToUTC"].isNull()) current_settings.minutesOffsetToUTC = doc["minutesOffsetToUTC"];
        if (!doc["brightness"].isNull()) current_settings.brightness = doc["brightness"];
        if (!doc["volume"].isNull()) current_settings.volume = doc["volume"];
        if (!doc["audio_gain"].isNull()) current_settings.audio_gain = doc["audio_gain"];
        if (!doc["cycle_count"].isNull()) current_settings.cycle_count = doc["cycle_count"];
        if (!doc["is_muted"].isNull()) current_settings.is_muted = doc["is_muted"];
        if (!doc["auto_tare_on"].isNull()) current_settings.auto_tare_on = doc["auto_tare_on"];
        if (!doc["audio_base_freq"].isNull()) current_settings.audio_base_freq = doc["audio_base_freq"];
        if (!doc["audio_max_freq"].isNull()) current_settings.audio_max_freq = doc["audio_max_freq"];
        if (!doc["audio_waveform"].isNull()) current_settings.audio_waveform = doc["audio_waveform"];
        if (!doc["wifi_ssid"].isNull()) strncpy(current_settings.wifi_ssid, doc["wifi_ssid"], sizeof(current_settings.wifi_ssid) - 1);
        if (!doc["wifi_password"].isNull()) strncpy(current_settings.wifi_password, doc["wifi_password"], sizeof(current_settings.wifi_password) - 1);
        if (!doc["enable_serial_logging"].isNull()) current_settings.enable_serial_logging = doc["enable_serial_logging"];
        if (!doc["timezonelabel"].isNull()) strncpy(current_settings.timezone_label, doc["timezonelabel"], sizeof(current_settings.timezone_label) - 1);
    }
    file.close();
}

extern "C" void save_settings(void) {
    File file = get_active_fs().open("/settings.json", "w");
    if (!file) return;
    JsonDocument doc;
    doc["minutesOffsetToUTC"] = current_settings.minutesOffsetToUTC;
    doc["brightness"] = current_settings.brightness;
    doc["volume"] = current_settings.volume;
    doc["audio_gain"] = current_settings.audio_gain;
    doc["cycle_count"] = current_settings.cycle_count;
    doc["is_muted"] = current_settings.is_muted;
    doc["auto_tare_on"] = current_settings.auto_tare_on;
    doc["audio_base_freq"] = current_settings.audio_base_freq;
    doc["audio_max_freq"] = current_settings.audio_max_freq;
    doc["audio_waveform"] = current_settings.audio_waveform;
    doc["wifi_ssid"] = current_settings.wifi_ssid;
    doc["wifi_password"] = current_settings.wifi_password;
    doc["enable_serial_logging"] = current_settings.enable_serial_logging;
    doc["timezonelabel"] = current_settings.timezone_label;
    
    String output;
    serializeJson(doc, output);
    file.print(output);
    file.close();
    if (sd_card_mounted) {
        Serial.println("Settings saved to SD Card.");
    } else {
        Serial.println("Settings saved to FFat.");
    }
}

// Days in month lookup (non-leap year)
static const uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static bool isLeapYear(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

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

WebServer server(80);

// Helpers
static void handleRoot() {
    String html = "<html><head><title>Magnetic Field Scanner Portal " + String(FIRMWARE_VERSION) + "</title><style>body{font-family:sans-serif;margin:20px;} li{margin:10px 0;} .up{margin-bottom:20px;padding:10px;background:#eee;}</style></head><body><h1>Magnetic Field Scanner Portal " + String(FIRMWARE_VERSION) + "</h1>";
    
    // HTML for Upload Form
    auto upload_html = [](const char* fs_param, const char* title) -> String {
        return "<div class='up'><h3>Upload to " + String(title) + "</h3><form method='POST' action='/upload?fs=" + String(fs_param) + "' enctype='multipart/form-data'><input type='file' name='f'> <input type='submit' value='Upload'></form></div>";
    };

    // Lambda to generate HTML for a given directory using pure POSIX to prevent 0-byte file creation bugs
    auto generate_dir_html = [](const char* title, const char* fs_param, const char* vfs_path) -> String {
        String out = "<h2>" + String(title) + "</h2><ul>";
        DIR *dir = opendir(vfs_path);
        if (!dir) {
            return out + "<li>ERROR: Failed to open directory via dirent (" + String(vfs_path) + ")</li></ul>";
        }
        
        int count = 0;
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            String filename = String(ent->d_name);
            if (filename == "." || filename == "..") continue;
            
            String full_path = String(vfs_path) + "/" + filename;
            struct stat st;
            if (stat(full_path.c_str(), &st) == 0) {
                if (S_ISREG(st.st_mode)) { // It's a regular file
                    size_t size = st.st_size;
                    String rel_path = "/" + filename;
                    String link_attrs;
                    String endpoint = "/download";
                    if (filename.endsWith(".bmp")) {
                        link_attrs = "target=\"_blank\"";
                        endpoint = "/view";
                    } else {
                        link_attrs = "download=\"" + filename + "\"";
                    }
                    out += "<li><a href=\"" + endpoint + "?fs=" + String(fs_param) + "&file=" + rel_path + "\" " + link_attrs + ">" + filename + "</a> (" + String(size) + " bytes) - <a href=\"/delete?fs=" + String(fs_param) + "&file=" + rel_path + "\">[Delete]</a></li>";
                    count++;
                }
            }
        }
        closedir(dir);
        if (count == 0) out += "<li>No files found.</li>";
        out += "</ul>";
        return out;
    };

    if (sd_card_mounted) {
        html += upload_html("sd", "SD Card");
        html += generate_dir_html("SD Card", "sd", "/sd");
    }
    
    html += upload_html("ffat", "Internal Flash (FFat)");
    html += generate_dir_html("Internal Flash (FFat)", "ffat", "/ffat");
    
    html += "</body></html>";
    server.send(200, "text/html", html);
}

static void handleSetTime() {
    if (server.hasArg("y") && server.hasArg("m") && server.hasArg("d") && server.hasArg("h") && server.hasArg("min") && server.hasArg("s")) {
        uint16_t y = server.arg("y").toInt();
        uint8_t m = server.arg("m").toInt();
        uint8_t d = server.arg("d").toInt();
        uint8_t h = server.arg("h").toInt();
        uint8_t min = server.arg("min").toInt();
        uint8_t s = server.arg("s").toInt();
        i2c_rtc_setTime(y, m, d, h, min, s);
    }
    server.send(200, "text/plain", "OK");
}

static void handleDownload() {
    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Missing file parameter");
        return;
    }
    String path = server.arg("file");
    String fs_type = server.hasArg("fs") ? server.arg("fs") : "ffat";
    
    fs::FS* target_fs = &FFat;
    if (fs_type == "sd" && sd_card_mounted) target_fs = &SD;

    if (!path.startsWith("/")) path = "/" + path;
    File file = target_fs->open(path, "r");
    if (!file) {
        server.send(404, "text/plain", "File not found");
        return;
    }
    
    int slashIndex = path.lastIndexOf('/');
    String filename = (slashIndex >= 0) ? path.substring(slashIndex + 1) : path;
    
    String mimeType = "application/octet-stream";
    String disposition = "attachment";
    
    if (filename.endsWith(".csv")) {
        mimeType = "text/csv";
    } else if (filename.endsWith(".bmp")) {
        mimeType = "image/bmp";
        disposition = "inline"; // Let the browser open it directly!
    } else if (filename.endsWith(".json")) {
        mimeType = "application/json";
    }
    
    server.sendHeader("Content-Disposition", disposition + "; filename=\"" + filename + "\"");
    server.sendHeader("Content-Length", String(file.size()));
    server.sendHeader("Connection", "close");
    server.streamFile(file, mimeType);
    file.close();
}

static void handleView() {
    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Missing file parameter");
        return;
    }
    String path = server.arg("file");
    String fs_type = server.hasArg("fs") ? server.arg("fs") : "ffat";
    
    int slashIndex = path.lastIndexOf('/');
    String filename = (slashIndex >= 0) ? path.substring(slashIndex + 1) : path;
    
    String html = "<html><head><title>View Image</title></head><body style='background:#1a1a1a;color:white;text-align:center;font-family:sans-serif;padding:20px;'>";
    html += "<button onclick='dl()' style='padding:15px 30px;font-size:18px;background:#007bff;color:white;border:none;border-radius:5px;cursor:pointer;'>Save Securely as PNG</button>";
    html += "<p style='color:#aaa;font-size:12px;margin-top:10px;margin-bottom:20px;'>This converts the internal BMP to a PNG using your browser, bypassing Chrome HTTP download warnings.</p>";
    html += "<img id='img' src='/download?fs=" + fs_type + "&file=" + path + "' style='max-width:100%; border:2px solid #555; border-radius:8px;'><br><br>";
    html += "<script>function dl(){";
    html += "var img = document.getElementById('img');";
    html += "var c = document.createElement('canvas'); c.width = img.naturalWidth; c.height = img.naturalHeight;";
    html += "c.getContext('2d').drawImage(img, 0, 0);";
    html += "var a = document.createElement('a'); a.download = '" + filename.substring(0, filename.length()-4) + ".png';";
    html += "a.href = c.toDataURL('image/png'); a.click();";
    html += "}</script></body></html>";
    
    server.send(200, "text/html", html);
}

static void handleDelete() {
    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Missing file parameter");
        return;
    }
    String path = server.arg("file");
    String fs_type = server.hasArg("fs") ? server.arg("fs") : "ffat";
    
    fs::FS* target_fs = &FFat;
    if (fs_type == "sd" && sd_card_mounted) target_fs = &SD;

    if (!path.startsWith("/")) path = "/" + path;
    target_fs->remove(path);
    server.sendHeader("Location", "/");
    server.send(303);
}

static File uploadFile;
static void handleUpload() {
    HTTPUpload& upload = server.upload();
    
    // Determine target fs from url query parameter, e.g. /upload?fs=sd
    String fs_type = server.hasArg("fs") ? server.arg("fs") : "ffat";
    fs::FS* target_fs = &FFat;
    if (fs_type == "sd" && sd_card_mounted) target_fs = &SD;

    if(upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        if(!filename.startsWith("/")) filename = "/" + filename;
        uploadFile = target_fs->open(filename, "w");
    } else if(upload.status == UPLOAD_FILE_WRITE) {
        if(uploadFile) uploadFile.write(upload.buf, upload.currentSize);
    } else if(upload.status == UPLOAD_FILE_END) {
        if(uploadFile) uploadFile.close();
        if(upload.filename.endsWith("calibration.json")) {
            load_calibration();
        }
    }
}


extern "C" void init_wifi_logger(void) {
    if (log_mux == NULL) {
        log_mux = xSemaphoreCreateMutex();
    }
    load_settings();
}

extern "C" void load_calibration(void) {
    File file = get_active_fs().open("/calibration.json", "r");
    // Smart Fallback: If using SD but cal doesn't exist yet, try to load from FFat
    if (!file && sd_card_mounted) {
        file = FFat.open("/calibration.json", "r");
        if (file) Serial.println("Loaded calibration.json from FFat fallback (will save to SD later)");
    }
    
    if (!file) {
        Serial.println("No calibration.json found. Creating default...");
        return;
    }
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    if (!err) {
        if(doc["ref_hard"].is<JsonArray>()) {
            JsonArray arr = doc["ref_hard"].as<JsonArray>();
            for(int i=0; i<3; i++) cal_config.ref_hard[i] = arr[i];
        }
        if(doc["ref_soft"].is<JsonArray>()) {
            JsonArray arr = doc["ref_soft"].as<JsonArray>();
            for(int i=0; i<3; i++) {
                JsonArray row = arr[i].as<JsonArray>();
                for(int j=0; j<3; j++) cal_config.ref_soft[i][j] = row[j];
            }
        }
        if(doc["tip_hard"].is<JsonArray>()) {
            JsonArray arr = doc["tip_hard"].as<JsonArray>();
            for(int i=0; i<3; i++) cal_config.tip_hard[i] = arr[i];
        }
        if(doc["tip_soft"].is<JsonArray>()) {
            JsonArray arr = doc["tip_soft"].as<JsonArray>();
            for(int i=0; i<3; i++) {
                JsonArray row = arr[i].as<JsonArray>();
                for(int j=0; j<3; j++) cal_config.tip_soft[i][j] = row[j];
            }
        }
    }
    file.close();
}

extern "C" void save_calibration(float ref_hard[3], float ref_soft[3][3], float tip_hard[3], float tip_soft[3][3]) {
    File file = get_active_fs().open("/calibration.json", "w");
    if (!file) {
        Serial.println("Failed to open calibration.json for writing");
        return;
    }
    
    JsonDocument doc;
    doc["calibration_type"] = "Magneto 1.2";
    
    JsonArray rh = doc["ref_hard"].to<JsonArray>();
    for(int i=0; i<3; i++) rh.add(ref_hard[i]);
    
    JsonArray rs = doc["ref_soft"].to<JsonArray>();
    for(int i=0; i<3; i++) {
        JsonArray row = rs.add<JsonArray>();
        for(int j=0; j<3; j++) row.add(ref_soft[i][j]);
    }
    
    JsonArray th = doc["tip_hard"].to<JsonArray>();
    for(int i=0; i<3; i++) th.add(tip_hard[i]);
    
    JsonArray ts = doc["tip_soft"].to<JsonArray>();
    for(int i=0; i<3; i++) {
        JsonArray row = ts.add<JsonArray>();
        for(int j=0; j<3; j++) row.add(tip_soft[i][j]);
    }
    
    serializeJson(doc, file);
    file.close();
    Serial.println("Calibration saved!");
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
    
    logFile.println("time_ms,timestamp,version,voltage,cc,refX_raw,refY_raw,refZ_raw,tipX_raw,tipY_raw,tipZ_raw,refX_cal,refY_cal,refZ_cal,tipX_cal,tipY_cal,tipZ_cal,calOffsetX,calOffsetY,calOffsetZ,gradX,gradY,gradZ,mag,nT,accX,accY,accZ,gyrX,gyrY,gyrZ,freq,is_muted");
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
    
    logFile.println("time_ms,timestamp,version,voltage,cc,refX_raw,refY_raw,refZ_raw,tipX_raw,tipY_raw,tipZ_raw,refX_cal,refY_cal,refZ_cal,tipX_cal,tipY_cal,tipZ_cal,calOffsetX,calOffsetY,calOffsetZ,gradX,gradY,gradZ,mag,nT,accX,accY,accZ,gyrX,gyrY,gyrZ,freq,is_muted");
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
        
        // Parse 33 columns. refX is idx 5, tipX is idx 8
        // time_ms,timestamp,version,voltage,cc,refX,refY,refZ,tipX,tipY,tipZ
        int col = 0;
        int start_idx = 0;
        int len = line.length();
        int32_t parsed_vals[11] = {0}; // We only care up to index 10
        
        for (int i = 0; i <= len && col < 11; i++) {
            if (i == len || line.charAt(i) == ',') {
                if (col >= 5 && col <= 10) {
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
        for (int i=5; i<=10; i++) {
            if (abs(parsed_vals[i]) > 50000) {
                is_valid = false;
                break;
            }
        }
        
        if (is_valid) {
            cal_ref_x[count] = parsed_vals[5];
            cal_ref_y[count] = parsed_vals[6];
            cal_ref_z[count] = parsed_vals[7];
            cal_tip_x[count] = parsed_vals[8];
            cal_tip_y[count] = parsed_vals[9];
            cal_tip_z[count] = parsed_vals[10];
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
                mat_mult_3x3(R_align, tip_soft, new_tip_soft);
                
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


extern "C" void log_data(uint32_t timestamp, float gain, int cc, int32_t refX_raw, int32_t refY_raw, int32_t refZ_raw, int32_t tipX_raw, int32_t tipY_raw, int32_t tipZ_raw, int32_t refX_cal, int32_t refY_cal, int32_t refZ_cal, int32_t tipX_cal, int32_t tipY_cal, int32_t tipZ_cal, int32_t calOffsetX, int32_t calOffsetY, int32_t calOffsetZ, int32_t gradX, int32_t gradY, int32_t gradZ, float mag, float nT, float accX, float accY, float accZ, float gyrX, float gyrY, float gyrZ, float freq, bool is_muted) {
    char ts[64];
    get_formatted_timestamp(ts, sizeof(ts), true);
    
    char buffer[512];
    snprintf(buffer, sizeof(buffer), 
             "%lu,%s,%s,%.1f,%d,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%.1f,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%d",
             timestamp, ts, FIRMWARE_VERSION, 3.0f, cc,
             refX_raw, refY_raw, refZ_raw,
             tipX_raw, tipY_raw, tipZ_raw,
             refX_cal, refY_cal, refZ_cal,
             tipX_cal, tipY_cal, tipZ_cal,
             calOffsetX, calOffsetY, calOffsetZ,
             gradX, gradY, gradZ,
             mag, nT,
             accX, accY, accZ,
             gyrX, gyrY, gyrZ,
             freq, is_muted ? 1 : 0);

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

extern "C" void update_wifi_ip_label(const char* ip);

extern "C" void start_wifi(void) {
    if (wifi_active) return;
    
    update_wifi_ip_label("Connecting...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(current_settings.wifi_ssid, current_settings.wifi_password);
    
    // Non-blocking connection task so UI stays responsive
    xTaskCreatePinnedToCore([](void* arg){
        int attempts = 0;
        while(WiFi.status() != WL_CONNECTED && attempts < 20) {
            vTaskDelay(pdMS_TO_TICKS(500));
            attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            wifi_active = true;
            String ip = WiFi.localIP().toString();
            update_wifi_ip_label(ip.c_str());
            
            // Auto-sync NTP to RTC (defaults to UTC, user can override via Web UI)
            configTime(0, 0, "pool.ntp.org");
            struct tm timeinfo;
            if (getLocalTime(&timeinfo, 10000)) { // wait up to 10 seconds for NTP
                i2c_rtc_setTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                Serial.println("RTC synced via NTP!");
            }
            
            server.on("/", handleRoot);
            server.on("/set_time", handleSetTime);
            server.on("/download", handleDownload);
            server.on("/view", handleView);
            server.on("/delete", handleDelete);
            server.on("/format", []() {
                FFat.format();
                server.sendHeader("Location", "/");
                server.send(303);
            });
            server.on("/upload", HTTP_POST, []() {
                server.sendHeader("Location", "/");
                server.send(303);
            }, handleUpload);
            
            server.begin();
            Serial.print("Wi-Fi Connected! IP: ");
            Serial.println(ip);
        } else {
            wifi_active = false;
            update_wifi_ip_label("Wi-Fi Failed");
            WiFi.disconnect();
        }
        vTaskDelete(NULL);
    }, "wifi_connect", 4096, NULL, 1, NULL, 0);
}

extern "C" void stop_wifi(void) {
    if (!wifi_active) return;
    server.stop();
    WiFi.disconnect(true);
    wifi_active = false;
    update_wifi_ip_label("");
    Serial.println("Wi-Fi Stopped.");
}

extern "C" void handle_wifi_server(void) {
    if (wifi_active) {
        server.handleClient();
    }
}

