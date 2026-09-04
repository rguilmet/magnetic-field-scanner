#include "web_server.h"
#include "../../user_config.h"
#include "../settings_manager/settings_manager.h"
#include "../data_logger/data_logger.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <FFat.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../PCF85063/rtc_bsp.h"
bool wifi_active = false;
WebServer server(80);
extern "C" void update_wifi_ip_label(const char* ip);
static void handleRoot() {
    String html = "<html><head><title>Magnetic Field Scanner Portal " + String(MFS_FIRMWARE_VERSION) + "</title><style>body{font-family:sans-serif;margin:20px;} li{margin:10px 0;} .up{margin-bottom:20px;padding:10px;background:#eee;}</style></head><body><h1>Magnetic Field Scanner Portal " + String(MFS_FIRMWARE_VERSION) + "</h1>";
    
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
