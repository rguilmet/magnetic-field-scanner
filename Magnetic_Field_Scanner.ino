#include <Arduino.h>

// Helper structure to cleanly bundle 3D vector points
struct MagData {
    int32_t x;
    int32_t y;
    int32_t z;
};
#include <math.h>
#include "user_config.h"
#include "src/imu_bsp/MadgwickAHRS.h"
#include "src/i2c_bsp/i2c_bsp.h"
#include "src/imu_bsp/imu_bsp.h"
#include "src/lvgl_port/lvgl_port.h"
#include "src/codec_board/codec_board.h"
#include "src/codec_board/codec_init.h"
#include "src/tca9554/esp_io_expander_tca9554.h"
#include "src/button_bsp/button_bsp.h"
#include "src/settings_manager/settings_manager.h"
#include "src/web_server/web_server.h"
#include "src/data_logger/data_logger.h"
#include "src/screenshot/screenshot.h"
#include "src/PCF85063/rtc_bsp.h"
#include "esp_adc/adc_oneshot.h"
#include <SD.h>
#include <FFat.h>
#include <SPI.h>

extern esp_io_expander_handle_t io_expander;
bool sd_card_mounted = false;
static bool is_vbatpowerflag = false;

// PIN Definitions

// Valid Hardwired 7-bit Addresses
#define ADDR_TIP        0x23
#define ADDR_REF        0x21
#define ADDR_MID        0x22

// ESP-IDF Hardware I2C Handles for RM3100s
i2c_master_dev_handle_t rm3100_tip_handle = NULL;
i2c_master_dev_handle_t rm3100_ref_handle = NULL;
i2c_master_dev_handle_t rm3100_mid_handle = NULL;

// RM3100 Internal Hardware Registers
#define REG_POLL        0x00
#define REG_CMM         0x01
#define REG_CCX         0x04 // Cycle Count Register start
#define REG_RESULTS     0x24
#define REG_TMRC        0x0B // First byte of X-axis output

// LEDC Backlight Control (Arduino wrapper for LVGL port)
extern "C" void init_backlight_pwm(void) {
    // ESP32 Arduino Core 3.x API
    ledcAttach(42, 5000, 8); // Pin 42, 5kHz, 8-bit
    ledcWrite(42, 128);      // Start at ~50%
}
extern "C" void set_backlight_pwm(uint8_t brightness) {
    ledcWrite(42, brightness);
}



// Audio Configuration
#define SAMPLE_RATE 24000
#define BEEP_DUR_MS 100
#define SAMPLES_PER_CH (SAMPLE_RATE * BEEP_DUR_MS / 1000)
#define NUM_CHANNELS 2

esp_codec_dev_handle_t playback = NULL;
adc_oneshot_unit_handle_t adc1_handle = NULL;
volatile float current_battery_voltage = 0.0f;

void task_battery_monitor(void *pvParameters) {
    (void) pvParameters;
    for (;;) {
        if (adc1_handle) {
            int raw_adc = 0;
            adc_oneshot_read(adc1_handle, ADC_CHANNEL_3, &raw_adc);
            current_battery_voltage = (raw_adc / 4095.0f) * 9.3f;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void audio_init(void) {
    if (io_expander != NULL) {
        esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_7, IO_EXPANDER_OUTPUT);
        esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_7, 1);
    }

    set_codec_board_type("S3_LCD_3_49");
    codec_init_cfg_t codec_cfg = {
        .in_mode = CODEC_I2S_MODE_TDM,
        .out_mode = CODEC_I2S_MODE_TDM,
        .in_use_tdm = false,
        .reuse_dev = false,
    };
    ESP_ERROR_CHECK(init_codec(&codec_cfg));
    playback = get_playback_handle();

    esp_codec_dev_set_out_vol(playback, 20.0); // Lowered default volume to 20%
    
    esp_codec_dev_sample_info_t fs = {};
    fs.sample_rate = SAMPLE_RATE;
    fs.channel = NUM_CHANNELS;
    fs.bits_per_sample = 16;
    esp_codec_dev_open(playback, &fs); 
}



// Queue for passing gradient magnitude data between tasks
QueueHandle_t gradientQueue;
QueueHandle_t audioQueue;

// DRDY Event Group for hardware interrupt synchronization
EventGroupHandle_t drdy_event_group;
#define DRDY_TIP_BIT (1 << 0)
#define DRDY_REF_BIT (1 << 1)



// Core state variables for settings
volatile bool is_scanning = true;
volatile bool is_muted = false;
volatile float current_audio_gain = 50.0f;
uint16_t current_cycle_count = 200;
volatile uint16_t pending_cycle_count = 0;
#include "src/sensor_fusion/SensorFusion.h"
SensorFusion sensorFusion;

extern "C" void set_auto_tare(bool enable) {
    sensorFusion.setAutoTareEnabled(enable);
}

extern "C" void trigger_manual_tare(void) {
    sensorFusion.setTareRequested();
}

extern "C" void reset_tare(void) {
    sensorFusion.resetTare();
}

// Initial setup to configure sensor settings using Hardware I2C
void initRM3100(i2c_master_dev_handle_t handle) {
    if (handle == NULL) return;

    // 1. Set the internal Cycle Counts based on the current setting
    uint8_t msb = (current_cycle_count >> 8) & 0xFF;
    uint8_t lsb = current_cycle_count & 0xFF;
    uint8_t ccx_data[] = {REG_CCX, msb, lsb, msb, lsb, msb, lsb};
    i2c_write_buff(handle, -1, ccx_data, sizeof(ccx_data));

    // 2. Set the proper TMRC (Update Rate) so measurements aren't truncated!
    uint8_t tmrc_val = 0x96; // 37Hz default
    if (current_cycle_count <= 200) tmrc_val = 0x94; // 150Hz
    else if (current_cycle_count <= 400) tmrc_val = 0x95; // 75Hz
    else if (current_cycle_count <= 800) tmrc_val = 0x96; // 37Hz
    else if (current_cycle_count <= 1600) tmrc_val = 0x97; // 18Hz
    else tmrc_val = 0x98; // 9Hz
    uint8_t tmrc_data[] = {REG_TMRC, tmrc_val};
    i2c_write_buff(handle, -1, tmrc_data, sizeof(tmrc_data));

    // 3. Enable Continuous Measurement Mode (CMM)
    uint8_t cmm_data[] = {REG_CMM, 0x7D};
    i2c_write_buff(handle, -1, cmm_data, sizeof(cmm_data));
}

// Dynamically change cycle count from Settings screen
extern "C" {
    void set_rm3100_cycle_count(uint16_t count) {
        // Defer the actual hardware update to the Sensor Task to avoid I2C bus collisions
        pending_cycle_count = count;
    }

    void toggle_scanning(bool scanning) {
        is_scanning = scanning;
    }

    void toggle_mute(bool muted) {
        is_muted = muted;
    }

    void set_audio_gain(int gain) {
        current_audio_gain = (float)gain;
    }

    void set_audio_volume(int vol) {
        if (playback) {
            esp_codec_dev_set_out_vol(playback, (float)vol);
        }
    }

    void calibrate_sensors() {
        // We will trigger a calibration on the next successful read
        sensorFusion.setTareRequested();
    }

    void reset_calibration() {
        sensorFusion.resetTare();
        Serial.println("Calibration reset! Showing raw readings.");
    }
}

// Low-level I2C multi-byte read controller using Hardware I2C
MagData readSensor(i2c_master_dev_handle_t handle) {
    MagData data = {0, 0, 0};
    uint8_t buffer[9] = {0};
    
    if (handle == NULL) return data;

    // Request 9 sequential bytes from the sensor array starting at REG_RESULTS
    if (i2c_read_buff(handle, REG_RESULTS, buffer, 9) == ESP_OK) {
        // Reassemble 24-bit signed big-endian integers into 32-bit variables
        data.x = (int32_t)((buffer[0] << 16) | (buffer[1] << 8) | buffer[2]);
        data.y = (int32_t)((buffer[3] << 16) | (buffer[4] << 8) | buffer[5]);
        data.z = (int32_t)((buffer[6] << 16) | (buffer[7] << 8) | buffer[8]);
        
        // Handle proper sign-extension for negative 24-bit values
        if (data.x & 0x00800000) data.x |= 0xFF000000;
        if (data.y & 0x00800000) data.y |= 0xFF000000;
        if (data.z & 0x00800000) data.z |= 0xFF000000;
    }
    return data;
}

// ---------------------------------------------------------
// FreeRTOS Tasks
// ---------------------------------------------------------


#include "src/matrix_math/matrix_math.h"

volatile bool is_calibrating = false;
volatile int force_audio_tone = 0;
volatile int calibration_point_count = 0;

int32_t *cal_ref_x = NULL;
int32_t *cal_ref_y = NULL;
int32_t *cal_ref_z = NULL;
int32_t *cal_tip_x = NULL;
int32_t *cal_tip_y = NULL;
int32_t *cal_tip_z = NULL;

extern "C" void start_on_wand_calibration(void) {
    if (is_calibrating) return;
    
    // Ensure any manual logging is stopped before we open calibration.csv
    stop_logging();
    
    calibration_point_count = 0;
    start_calibration_logging();
    is_calibrating = true;
}

extern "C" void stop_on_wand_calibration(void) {
    if (!is_calibrating) return;
    is_calibrating = false;
    stop_logging(); // This closes the file
    rename_calibration_file_to_stopped();
}

void task_sensor_read(void *pvParameters) {
    (void) pvParameters;
    
    int consecutive_same_reads = 0;
    MagData last_tip = {0,0,0};
    MagData last_ref = {0,0,0};
    TickType_t last_read_time = xTaskGetTickCount();
    
    for (;;) {
        // Handle deferred cycle count updates
        if (pending_cycle_count != 0) {
            uint16_t old_count = current_cycle_count;
            uint16_t count = pending_cycle_count;
            pending_cycle_count = 0;
            current_cycle_count = count;

            Serial.printf("Stopping CMM to update cycle count to %d...\n", count);
            
            // 1. Fully Disable CMM
            uint8_t cmm_disable[] = {REG_CMM, 0x00}; 
            i2c_write_buff(rm3100_tip_handle, -1, cmm_disable, sizeof(cmm_disable));
            i2c_write_buff(rm3100_ref_handle, -1, cmm_disable, sizeof(cmm_disable));

            // 2. Wait 150ms to ensure any internal operations completely cease
            vTaskDelay(pdMS_TO_TICKS(150));

            // 3. Perform a dummy read to clear any stale DRDY on both sensors!
            uint8_t measure_data[9];
            i2c_read_buff(rm3100_tip_handle, REG_RESULTS, measure_data, 9);
            i2c_read_buff(rm3100_ref_handle, REG_RESULTS, measure_data, 9);

            // 4. Fully re-initialize both sensors using the boot sequence logic!
            initRM3100(rm3100_tip_handle);
            initRM3100(rm3100_ref_handle);

            // 5. Discard the first 3 measurements to let the sensor stabilize at the new Cycle Count!
            // 5. Discard the first 3 measurements to let the sensor stabilize at the new Cycle Count!
            xEventGroupClearBits(drdy_event_group, DRDY_TIP_BIT | DRDY_REF_BIT);
            for (int i = 0; i < 3; i++) {
                EventBits_t dummy_bits = xEventGroupWaitBits(drdy_event_group, DRDY_TIP_BIT | DRDY_REF_BIT, pdTRUE, pdTRUE, pdMS_TO_TICKS(500));
                if ((dummy_bits & (DRDY_TIP_BIT | DRDY_REF_BIT)) == (DRDY_TIP_BIT | DRDY_REF_BIT)) {
                    uint8_t dummy[9];
                    i2c_read_buff(rm3100_tip_handle, REG_RESULTS, dummy, 9);
                    i2c_read_buff(rm3100_ref_handle, REG_RESULTS, dummy, 9);
                }
            }

            // CC Scaling has been removed in v5.0.0. The calibration matrix is now completely unit-independent (nT).
            // No need to touch cal_config or Tare offsets when cycle count changes!
            
            Serial.println("Cycle count update complete. CMM restarted.");
        }

        if (!is_scanning) {
            vTaskDelay(pdMS_TO_TICKS(100));
            // Keep resetting timeout so watchdog doesn't trip while paused
            xEventGroupClearBits(drdy_event_group, DRDY_TIP_BIT | DRDY_REF_BIT);
            last_read_time = xTaskGetTickCount();
            continue;
        }

        // FIX for Lapping Lockup:
        // If DRDY is already HIGH because the SD card blocked and lapped the ISR, the RISING edge will never trigger!
        if (digitalRead(MFS_PIN_RM3100_TIP_DRDY) == HIGH) {
            xEventGroupSetBits(drdy_event_group, DRDY_TIP_BIT);
        }
        if (digitalRead(MFS_PIN_RM3100_REF_DRDY) == HIGH) {
            xEventGroupSetBits(drdy_event_group, DRDY_REF_BIT);
        }

        // True ISR-driven wait (max 500ms timeout for watchdog)
        EventBits_t bits = xEventGroupWaitBits(drdy_event_group, DRDY_TIP_BIT | DRDY_REF_BIT, pdTRUE, pdTRUE, pdMS_TO_TICKS(500));
        
        if ((bits & (DRDY_TIP_BIT | DRDY_REF_BIT)) == (DRDY_TIP_BIT | DRDY_REF_BIT)) {
            MagData tip = readSensor(rm3100_tip_handle);
            MagData ref = readSensor(rm3100_ref_handle);
            
            // Watchdog: Check for stuck sensor or I2C read failure
            if ((tip.x == last_tip.x && tip.y == last_tip.y && tip.z == last_tip.z) || 
                (tip.x == 0 && tip.y == 0 && tip.z == 0)) {
                consecutive_same_reads++;
            } else {
                consecutive_same_reads = 0;
            }
            
            // EMI Slew-Rate Filter
            // A jump of > 800 counts in a single 2.5ms frame is physically impossible unless struck by a magnet.
            // This elegantly drops single-bit I2C EMI flips (like the 2000-count jumps) without hard limits.
            bool is_first_frame = (last_tip.x == 0 && last_tip.y == 0 && last_tip.z == 0);
            bool slew_glitch = false;
            
            if (!is_first_frame) {
                if (abs(tip.x - last_tip.x) > MFS_SLEW_RATE_THRESHOLD || abs(tip.y - last_tip.y) > MFS_SLEW_RATE_THRESHOLD || abs(tip.z - last_tip.z) > MFS_SLEW_RATE_THRESHOLD ||
                    abs(ref.x - last_ref.x) > MFS_SLEW_RATE_THRESHOLD || abs(ref.y - last_ref.y) > MFS_SLEW_RATE_THRESHOLD || abs(ref.z - last_ref.z) > MFS_SLEW_RATE_THRESHOLD) {
                    slew_glitch = true;
                }
            }
            
            last_tip = tip;
            last_ref = ref;
            
            if (slew_glitch) {
                continue; // Silently drop this glitch frame and the recovery frame
            }

            // If stuck or failing I2C reads for 10 consecutive frames (true lockup)
            if (consecutive_same_reads > 10) {
                Serial.println("Sensor lockup detected (stuck data or dead I2C)! Resetting...");
                
                // Perform dummy reads to forcefully clear stuck DRDY pins before re-initializing
                uint8_t dummy[9];
                i2c_read_buff(rm3100_tip_handle, REG_RESULTS, dummy, 9);
                i2c_read_buff(rm3100_ref_handle, REG_RESULTS, dummy, 9);
                
                initRM3100(rm3100_tip_handle);
                initRM3100(rm3100_ref_handle);
                consecutive_same_reads = 0;
                vTaskDelay(pdMS_TO_TICKS(100)); // Give it time to reset
                last_read_time = xTaskGetTickCount();
                continue; // Skip processing this bad frame
            }
            
            // If it was just a single I2C read glitch (0,0,0), ignore it and grab next frame
            if (tip.x == 0 && tip.y == 0 && tip.z == 0) {
                continue;
            }
            
            // Absolute EMI Filter (for massive meteors that somehow sneak past slew rate)
            if (abs(tip.x) > MFS_MAX_GLITCH_MAGNITUDE || abs(tip.y) > MFS_MAX_GLITCH_MAGNITUDE || abs(tip.z) > MFS_MAX_GLITCH_MAGNITUDE ||
                abs(ref.x) > MFS_MAX_GLITCH_MAGNITUDE || abs(ref.y) > MFS_MAX_GLITCH_MAGNITUDE || abs(ref.z) > MFS_MAX_GLITCH_MAGNITUDE) {
                continue; 
            }
            
            last_read_time = xTaskGetTickCount();
            
            // Save raw values before modifying them for logging
            int32_t tip_raw_x = tip.x;
            int32_t tip_raw_y = tip.y;
            int32_t tip_raw_z = tip.z;
            int32_t ref_raw_x = ref.x;
            int32_t ref_raw_y = ref.y;
            int32_t ref_raw_z = ref.z;
            
            Vector3Float tip_vec = { (float)tip.x, (float)tip.y, (float)tip.z };
            Vector3Float ref_vec = { (float)ref.x, (float)ref.y, (float)ref.z };

            float raw_acc[3], raw_gyr[3];
            int16_t imu_temp = 0;
            imu_read(raw_acc, raw_gyr, &imu_temp);
            
            SensorFusionOutput out = sensorFusion.processUpdate(tip_vec, ref_vec, raw_acc, raw_gyr, current_cycle_count);
            
            // If manual tare was just completed, processUpdate returns empty output. Skip this frame.
            if (out.magnitude == 0 && out.nt_value == 0 && out.true_Z == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            float gradX = out.gradX;
            float gradY = out.gradY;
            float gradZ = out.gradZ;
            float magnitude = out.magnitude;
            float nt_value = out.nt_value;

            UIData ui_data;
            ui_data.cal_progress = 0;
            
            float raw_gx = (float)(tip_raw_x - ref_raw_x);
            float raw_gy = (float)(tip_raw_y - ref_raw_y);
            float raw_gz = (float)(tip_raw_z - ref_raw_z);
            ui_data.mag = sqrtf(raw_gx*raw_gx + raw_gy*raw_gy + raw_gz*raw_gz);
            
            ui_data.nt = out.nt_value;
            ui_data.gradX = out.right_grad;
            ui_data.gradY = out.fwd_grad;
            ui_data.trueZ = out.true_Z;
            ui_data.azimuth = out.azimuth;
            ui_data.elevation = out.elevation;
            ui_data.is_pin = out.is_pin;
            
            ui_data.tare_active = sensorFusion.isTareActive();
            ui_data.auto_tare_on = sensorFusion.isAutoTareEnabled();
            ui_data.battery_voltage = current_battery_voltage;
            
            Vector3Float calibration_offset = sensorFusion.getCalibrationOffset();

            xQueueOverwrite(audioQueue, &magnitude);
            
            // Re-calculate the audio target frequency for logging
            float target_freq = 40.0f;
            if (nt_value > MFS_AUDIO_SQUELCH_NT) {
                float gain_multiplier = 0.0666f * expf(0.05416f * current_audio_gain);
                target_freq = 40.0f + ((nt_value - MFS_AUDIO_SQUELCH_NT) * gain_multiplier);
            }
            if (target_freq > 3000.0f) target_freq = 3000.0f;

            log_data(millis(), current_battery_voltage, current_audio_gain, current_cycle_count, ref_raw_x, ref_raw_y, ref_raw_z, tip_raw_x, tip_raw_y, tip_raw_z, ref_vec.x, ref_vec.y, ref_vec.z, tip_vec.x, tip_vec.y, tip_vec.z, calibration_offset.x, calibration_offset.y, calibration_offset.z, out.gradX, out.gradY, out.gradZ, magnitude, nt_value, raw_acc[0], raw_acc[1], raw_acc[2], raw_gyr[0], raw_gyr[1], raw_gyr[2], imu_temp, target_freq, is_muted, q0, q1, q2, q3, ui_data.azimuth, ui_data.elevation, current_settings.mag_declination_deg);

            // --- ON-WAND CALIBRATION LOGIC ---
            if (is_calibrating) {
                if (calibration_point_count < CALIBRATION_POINTS) {
                                        if (calibration_point_count % 600 == 0 && calibration_point_count != 0) {
                        force_audio_tone = 1000;
                    } else if (calibration_point_count % 600 == 15) {
                        force_audio_tone = 0;
                    }
                    
                    calibration_point_count = calibration_point_count + 1;
                    ui_data.cal_progress = (calibration_point_count * 100) / CALIBRATION_POINTS;
                }
                if (calibration_point_count >= CALIBRATION_POINTS) {
                    force_audio_tone = 1500;
                    is_calibrating = false;
                    stop_logging();
                    Serial.println("Calibration data gathered, launching processing task...");
                    
                    xTaskCreatePinnedToCore(
                        [](void *arg) {
                            bool success = process_calibration_file();
                            show_calibration_result_msg(success);
                            vTaskDelete(NULL);
                        },
                        "CalProcess",
                        8192,
                        NULL,
                        1,
                        NULL,
                        0
                    );
                    
                    vTaskDelay(pdMS_TO_TICKS(500)); // Hold completion beep for half a second
                    force_audio_tone = 0;
                    ui_data.cal_progress = 100; // Force modal to close
                }
            }

            
            // Note: log_data now handles writing the CSV string to the Serial port for live monitoring.
            
            xQueueOverwrite(gradientQueue, &ui_data);
        }
        else {
            // Watchdog: Check for timeout lockup (no DRDY for 500ms)
            Serial.println("Sensor lockup detected (DRDY timeout)! Resetting...");
            
            // Perform dummy reads to forcefully clear stuck DRDY pins before re-initializing
            uint8_t dummy[9];
            i2c_read_buff(rm3100_tip_handle, REG_RESULTS, dummy, 9);
            i2c_read_buff(rm3100_ref_handle, REG_RESULTS, dummy, 9);
            
            initRM3100(rm3100_tip_handle);
            initRM3100(rm3100_ref_handle);
            last_read_time = xTaskGetTickCount(); // Reset timeout
        }
    }
}

void task_display_update(void *pvParameters) {
    (void) pvParameters;
    UIData ui_data;
    ui_data.cal_progress = 0;
    
    for (;;) {
        // Auto-save debounce
        if (settings_dirty && (millis() - last_settings_change_ms > 5000)) {
            save_settings();
            settings_dirty = false;
        }
        // Wait for new data from the queue, blocking indefinitely
        if (xQueueReceive(gradientQueue, &ui_data, portMAX_DELAY) == pdPASS) {
            update_detector_ui(&ui_data);
        }
        // Limit UI updates to ~30 FPS to prevent locking up LVGL's renderer
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

void task_audio_alert(void *pvParameters) {
    (void) pvParameters;
    float currentMagnitude = 0.0f;
    float current_audio_nt = 0.0f;
    float smoothed_freq = 0.0f; 
    float phase = 0.0f;
    const int AUDIO_CHUNK = 512;
    int16_t stream_buf[AUDIO_CHUNK * 2]; // Stereo buffer
    
    for (;;) {
        // Non-blocking read of the latest magnetic magnitude
        if (xQueueReceive(audioQueue, &currentMagnitude, 0) == pdPASS) {
            if (current_cycle_count > 0) {
                current_audio_nt = (currentMagnitude * 1000.0f) / (0.38f * (float)current_cycle_count);
            }
        }
        
        float target_freq = 40.0f;
        
        if (force_audio_tone > 0) {
            target_freq = (float)force_audio_tone;
        } else {
            if (!is_scanning || is_muted) {
                memset(stream_buf, 0, sizeof(stream_buf));
                esp_codec_dev_write(playback, stream_buf, sizeof(stream_buf));
                smoothed_freq = 0.0f;
                continue;
            }

            // Squelch
            if (current_audio_nt > 20.0f) {
                float gain_multiplier = 0.0666f * expf(0.05416f * current_audio_gain);
                target_freq = 40.0f + ((current_audio_nt - 20.0f) * gain_multiplier);
            }
        }
        
        if (target_freq > 3000.0f) target_freq = 3000.0f; // Hard cap
        
        // EMA smoothing to remove raw sensor jitter (Bypass if forcing tone)
        if (smoothed_freq == 0.0f || force_audio_tone > 0) {
            smoothed_freq = target_freq;
        } else {
            smoothed_freq = (0.2f * target_freq) + (0.8f * smoothed_freq);
        }
        
        float actual_freq = smoothed_freq;
        if (current_settings.audio_waveform == 3 && force_audio_tone == 0) {
            actual_freq = smoothed_freq / 20.0f; // Scale 40Hz-3000Hz down to 2Hz-150Hz for discrete clicks
        }
        
        float phase_increment = 2.0f * (float)M_PI * actual_freq / (float)SAMPLE_RATE;
        
        static int click_samples_remaining = 0;
        
        for (int i = 0; i < AUDIO_CHUNK; i++) {
            int16_t sample = 0;
            
            if (current_settings.audio_waveform == 3 && force_audio_tone == 0) { 
                // Geiger Counter Mode (Impulse waveform)
                if (click_samples_remaining > 0) {
                    // Drive speaker cone back and forth violently for maximum RMS volume during the transient
                    sample = (click_samples_remaining % 2 == 0) ? 32767 : -32767;
                    click_samples_remaining--;
                } else {
                    sample = 0; // Absolute silence between ticks
                }
            } else { // SQR, TRI, SIN (and override tones)
                if (current_settings.audio_waveform == 0 || (current_settings.audio_waveform == 3 && force_audio_tone > 0)) { 
                    // Square (Fallback for GCM if forcing a tone, e.g., camera click)
                    sample = (phase < (float)M_PI) ? 8192 : -8192; 
                } else if (current_settings.audio_waveform == 1) { // Triangle
                    float t = phase / (2.0f * (float)M_PI);
                    sample = (int16_t)(24000.0f * (2.0f * fabs(2.0f * t - 1.0f) - 1.0f));
                } else { // Sine
                    sample = (int16_t)(32767.0f * sinf(phase));
                }
            }
            
            stream_buf[i * 2] = sample;     // Left
            stream_buf[i * 2 + 1] = sample; // Right
            
            phase += phase_increment;
            if (phase >= 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
                if (current_settings.audio_waveform == 3 && force_audio_tone == 0) {
                    // Trigger a 2ms transient click exactly once per cycle (wraparound)
                    click_samples_remaining = (int)(SAMPLE_RATE * 0.002f); 
                }
            }
        }
        
        // Blocks until I2S DMA has space
        esp_codec_dev_write(playback, stream_buf, sizeof(stream_buf));
    }
}

static void mfs_button_pwr_task(void* arg)
{
  (void) arg;
  for (;;)
  {
    EventBits_t even = xEventGroupWaitBits(pwr_groups, set_bit_all, pdTRUE, pdFALSE, pdMS_TO_TICKS(2 * 1000));
    if(get_bit_button(even,1)) // Long press
    {
        if(is_vbatpowerflag)
        {
            is_vbatpowerflag = false;
            ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, MFS_EXIO_PIN_SYS_EN, 0));
        }
    }
    else if(get_bit_button(even,2))
    {
        if(!is_vbatpowerflag)
        {
            is_vbatpowerflag = true;
        }
    }
  }
}

void power_Test(void *arg)
{
    (void) arg;
    if(digitalRead(MFS_PIN_NUM_SYS_OUT) == HIGH)
    {
        is_vbatpowerflag = true;
    }
    vTaskDelete(NULL); 
}

// Hardware Interrupt Handlers for DRDY
void IRAM_ATTR drdy_tip_isr() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xEventGroupSetBitsFromISR(drdy_event_group, DRDY_TIP_BIT, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void IRAM_ATTR drdy_ref_isr() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xEventGroupSetBitsFromISR(drdy_event_group, DRDY_REF_BIT, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void setup() {
    Serial.begin(921600);
    pinMode(0, INPUT_PULLUP); // BOOT button for screenshots
    
    // =========================================================================
    // CRITICAL: BATTERY LATCH INITIALIZATION
    // We must assert SYS_EN high IMMEDIATELY on boot. If we don't, the user 
    // has to physically hold the power button for the entire 3000ms Serial 
    // timeout delay below to keep the device alive!
    // =========================================================================
    i2c_master_Init(); // Boot I2C hardware bus for IO Expander
    i2c_master_bus_handle_t tca9554_i2c_bus = NULL;
    ESP_ERROR_CHECK(i2c_master_get_bus_handle(0, &tca9554_i2c_bus));
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(tca9554_i2c_bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, MFS_EXIO_PIN_SYS_EN, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, MFS_EXIO_PIN_SYS_EN, 1)); // Latch battery power ON!

    // Wait for native USB Serial to connect, with a 3 second timeout 
    // so the wand still boots instantly on battery power without a PC!
    unsigned long start_time = millis();
    while (!Serial && (millis() - start_time) < 3000) {
        delay(10);
    }
    if (Serial) {
        delay(500); // Give the Arduino IDE Serial Monitor a half-second to fully render
    }
    
    pinMode(MFS_PIN_RM3100_TIP_DRDY, INPUT);
    pinMode(MFS_PIN_RM3100_REF_DRDY, INPUT);
    pinMode(MFS_PIN_RM3100_MID_DRDY, INPUT);

    drdy_event_group = xEventGroupCreate();
    attachInterrupt(digitalPinToInterrupt(MFS_PIN_RM3100_TIP_DRDY), drdy_tip_isr, RISING);
    attachInterrupt(digitalPinToInterrupt(MFS_PIN_RM3100_REF_DRDY), drdy_ref_isr, RISING);

    Serial.printf("\n--- Magnetic Field Scanner %s ---\n", FIRMWARE_VERSION);

    // Register RM3100s on the shared hardware I2C bus (user_i2c_port0_handle)
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ADDR_TIP,
        .scl_speed_hz = 200000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg, &rm3100_tip_handle));
    dev_cfg.device_address = ADDR_REF;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg, &rm3100_ref_handle));
    dev_cfg.device_address = ADDR_MID;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg, &rm3100_mid_handle));

    rtc_init(); // Uses hardware I2C
    imu_init();
    // Phase 1 SD Card Testing (IO38=CS, IO39=MOSI, IO40=MISO, IO41=SCLK)
    if(!FFat.begin(true)) {
        Serial.println("FFat Mount Failed");
    } else {
        Serial.println("FFat Mounted");
    }
    SPI.begin(MFS_PIN_SD_SCLK, MFS_PIN_SD_MISO, MFS_PIN_SD_MOSI, MFS_PIN_SD_CS);
    if (!SD.begin(MFS_PIN_SD_CS, SPI, 4000000)) {
        Serial.println("SD Card Mount Failed. Please check card or wiring.");
    } else {
        sd_card_mounted = true;
        uint8_t cardType = SD.cardType();
        if (cardType == CARD_NONE) {
            Serial.println("No SD card attached (Card Type None).");
        } else {
            Serial.print("SD Card Type: ");
            if (cardType == CARD_MMC) Serial.println("MMC");
            else if (cardType == CARD_SD) Serial.println("SDSC");
            else if (cardType == CARD_SDHC) Serial.println("SDHC");
            else Serial.println("UNKNOWN");
            uint64_t cardSize = SD.cardSize() / (1024 * 1024);
            Serial.printf("SD Card Size: %lluMB\n", cardSize);
        }
    }
    init_wifi_logger();
    load_settings();
    load_calibration();
    sensorFusion.init(&cal_config, &current_settings);
    lvgl_port_init();  // LCD and LVGL init (creates lvgl_mux)
    start_wifi(); // Auto-start Wi-Fi on boot (uses lvgl_mux)
    
    // Initialize Audio
    audio_init();
    set_audio_volume(current_settings.volume);

    // Initialize ADC for Battery Monitoring (IO4 / ADC1_CH3)
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .clk_src = (adc_oneshot_clk_src_t)0,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_new_unit(&init_config1, &adc1_handle);

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_3, &config);

    


    pinMode(MFS_PIN_NUM_SYS_OUT, INPUT);
    button_Init();

    // Power tasks
    xTaskCreatePinnedToCore(mfs_button_pwr_task, "mfs_button_pwr_task", 4 * 1024, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(power_Test, "power_Test", 4 * 1024, NULL, 3, NULL, 1);

    Serial.println("Configuring Wand Sensors on Hardware I2C Bus...");

    initRM3100(rm3100_tip_handle);
    initRM3100(rm3100_ref_handle);
    Serial.println("Both sensors ready. Scanning armed.");

    // Create FreeRTOS Queues to hold the gradient magnitude (size 1 for xQueueOverwrite)
    gradientQueue = xQueueCreate(1, sizeof(UIData));
    audioQueue = xQueueCreate(1, sizeof(float));
    if (gradientQueue == NULL || audioQueue == NULL) {
        Serial.println("Failed to create queues");
    }

    // Start FreeRTOS Tasks
    // Move Sensor task to Core 0 to isolate software I2C bit-banging from UI
    xTaskCreatePinnedToCore(task_sensor_read, "SensorTask", 4096, NULL, 2, NULL, 0); 
    xTaskCreatePinnedToCore(task_display_update, "DisplayTask", 8192, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(task_audio_alert, "AudioTask", 4096, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(task_battery_monitor, "BatteryTask", 2048, NULL, 1, NULL, 1);
}

void loop() {
    handle_wifi_server();
    
    // BOOT Button Screenshot Trigger
    static uint32_t btn_press_start = 0;
    static bool btn_is_pressed = false;

    if (digitalRead(0) == LOW) {
        if (!btn_is_pressed) {
            btn_is_pressed = true;
            btn_press_start = millis();
        } else if (millis() - btn_press_start > 1500) {
            // Long Press Detected (1.5 seconds)
            // 1. Camera Click Audio (double beep) using the audio task override
            force_audio_tone = 2000;
            vTaskDelay(pdMS_TO_TICKS(50));
            force_audio_tone = 0;
            vTaskDelay(pdMS_TO_TICKS(50));
            force_audio_tone = 2500;
            vTaskDelay(pdMS_TO_TICKS(100));
            force_audio_tone = 0;
            
            // 2. Take Screenshot
            take_screenshot_to_sd();
            
            // 3. Debounce (prevent rapid fire)
            btn_press_start = millis() + 5000; 
        }
    } else {
        btn_is_pressed = false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}
