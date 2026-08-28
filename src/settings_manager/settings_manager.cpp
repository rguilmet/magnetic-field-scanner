#include "settings_manager.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <FFat.h>
extern SemaphoreHandle_t log_mux;
fs::FS& get_active_fs() {
    if (sd_card_mounted) {
        return SD;
    }
    return FFat;
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
