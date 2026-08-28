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



// Core state variables for settings
volatile bool is_scanning = true;
volatile bool is_muted = false;
volatile float current_audio_gain = 50.0f;
uint16_t current_cycle_count = 200;
volatile uint16_t pending_cycle_count = 0;
MagData calibration_offset = {0, 0, 0};

volatile bool auto_tare_enabled = false;
float auto_tare_x = 0.0f;
float auto_tare_y = 0.0f;
float auto_tare_z = 0.0f;

extern "C" void set_auto_tare(bool enable) {
    auto_tare_enabled = enable;
}

volatile bool tare_requested = false;

extern "C" void trigger_manual_tare(void) {
    tare_requested = true;
}

extern "C" void reset_tare(void) {
    calibration_offset.x = 0;
    calibration_offset.y = 0;
    calibration_offset.z = 0;
    auto_tare_x = 0.0f;
    auto_tare_y = 0.0f;
    auto_tare_z = 0.0f;
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
    if (current_cycle_count <= 50) tmrc_val = 0x94; // 150Hz
    else if (current_cycle_count <= 100) tmrc_val = 0x95; // 75Hz
    else if (current_cycle_count <= 200) tmrc_val = 0x96; // 37Hz
    else if (current_cycle_count <= 400) tmrc_val = 0x97; // 18Hz
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
        tare_requested = true;
    }

    void reset_calibration() {
        calibration_offset.x = 0;
        calibration_offset.y = 0;
        calibration_offset.z = 0;
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
            for (int i = 0; i < 3; i++) {
                uint32_t flush_start = millis();
                // Wait for DRDY
                while ((digitalRead(MFS_PIN_RM3100_TIP_DRDY) == LOW || digitalRead(MFS_PIN_RM3100_REF_DRDY) == LOW) && (millis() - flush_start < 200)) {
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
                uint8_t dummy[9];
                i2c_read_buff(rm3100_tip_handle, REG_RESULTS, dummy, 9);
                i2c_read_buff(rm3100_ref_handle, REG_RESULTS, dummy, 9);
            }

            // 6. Scale calibration offsets to match the new gain!
            // This prevents an automatic Tare and perfectly preserves the current baseline.
            if (old_count > 0) {
                float scale = (float)count / (float)old_count;
                
                // Avoid scaling if a tare is currently requested and hasn't been processed yet
                if (!tare_requested) {
                    calibration_offset.x = (int32_t)((float)calibration_offset.x * scale);
                    calibration_offset.y = (int32_t)((float)calibration_offset.y * scale);
                    calibration_offset.z = (int32_t)((float)calibration_offset.z * scale);
                }
                
                // Auto-scale the Hard-Iron calibration matrix centers!
                for(int i=0; i<3; i++) {
                    cal_config.tip_hard[i] *= scale;
                    cal_config.ref_hard[i] *= scale;
                }
                auto_tare_x *= scale;
                auto_tare_y *= scale;
                auto_tare_z *= scale;
            }
            
            Serial.println("Cycle count update complete. CMM restarted.");
        }

        if (!is_scanning) {
            vTaskDelay(pdMS_TO_TICKS(100));
            // Keep resetting timeout so watchdog doesn't trip while paused
            last_read_time = xTaskGetTickCount();
            continue;
        }

        // Wait for both sensors to be ready
        if (digitalRead(MFS_PIN_RM3100_TIP_DRDY) == HIGH && digitalRead(MFS_PIN_RM3100_REF_DRDY) == HIGH) {
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
            
            // Apply Hard/Soft Iron Calibration to Tip
            float tx = (float)tip.x - cal_config.tip_hard[0];
            float ty = (float)tip.y - cal_config.tip_hard[1];
            float tz = (float)tip.z - cal_config.tip_hard[2];
            
            tip.x = (int32_t)(tx * cal_config.tip_soft[0][0] + ty * cal_config.tip_soft[0][1] + tz * cal_config.tip_soft[0][2]);
            tip.y = (int32_t)(tx * cal_config.tip_soft[1][0] + ty * cal_config.tip_soft[1][1] + tz * cal_config.tip_soft[1][2]);
            tip.z = (int32_t)(tx * cal_config.tip_soft[2][0] + ty * cal_config.tip_soft[2][1] + tz * cal_config.tip_soft[2][2]);

            // Apply Hard/Soft Iron Calibration to Ref
            float rx = (float)ref.x - cal_config.ref_hard[0];
            float ry = (float)ref.y - cal_config.ref_hard[1];
            float rz = (float)ref.z - cal_config.ref_hard[2];

            ref.x = (int32_t)(rx * cal_config.ref_soft[0][0] + ry * cal_config.ref_soft[0][1] + rz * cal_config.ref_soft[0][2]);
            ref.y = (int32_t)(rx * cal_config.ref_soft[1][0] + ry * cal_config.ref_soft[1][1] + rz * cal_config.ref_soft[1][2]);
            ref.z = (int32_t)(rx * cal_config.ref_soft[2][0] + ry * cal_config.ref_soft[2][1] + rz * cal_config.ref_soft[2][2]);
            // Handle active manual calibration (tare) request
            int32_t raw_gradX = tip.x - ref.x;
            int32_t raw_gradY = tip.y - ref.y;
            int32_t raw_gradZ = tip.z - ref.z;

            if (tare_requested) {
                calibration_offset.x = raw_gradX;
                calibration_offset.y = raw_gradY;
                calibration_offset.z = raw_gradZ;
                auto_tare_x = 0.0f;
                auto_tare_y = 0.0f;
                auto_tare_z = 0.0f;
                tare_requested = false;
                Serial.println("Manual Tare complete! Baseline zeroed.");
                vTaskDelay(pdMS_TO_TICKS(10));
                continue; 
            }

            // Auto-Tare logic
            if (auto_tare_enabled) {
                float dx = (float)raw_gradX - (float)calibration_offset.x - auto_tare_x;
                float dy = (float)raw_gradY - (float)calibration_offset.y - auto_tare_y;
                float dz = (float)raw_gradZ - (float)calibration_offset.z - auto_tare_z;
                float current_mag = sqrtf(dx*dx + dy*dy + dz*dz);
                
                // If it's a slow drift (magnitude < ~50 counts jump), smoothly update the auto-tare
                if (current_mag < MFS_AUTO_TARE_THRESHOLD) {
                    float base_x = (float)raw_gradX - (float)calibration_offset.x;
                    float base_y = (float)raw_gradY - (float)calibration_offset.y;
                    float base_z = (float)raw_gradZ - (float)calibration_offset.z;
                    // Low pass filter
                    auto_tare_x = (auto_tare_x * MFS_EMA_ALPHA) + (base_x * (1.0f - MFS_EMA_ALPHA));
                    auto_tare_y = (auto_tare_y * MFS_EMA_ALPHA) + (base_y * (1.0f - MFS_EMA_ALPHA));
                    auto_tare_z = (auto_tare_z * MFS_EMA_ALPHA) + (base_z * (1.0f - MFS_EMA_ALPHA));
                }
            }
            
            // Calculate the physical delta gradient across all axes, minus calibration offsets
            int32_t gradX = raw_gradX - calibration_offset.x - (int32_t)auto_tare_x;
            int32_t gradY = raw_gradY - calibration_offset.y - (int32_t)auto_tare_y;
            int32_t gradZ = raw_gradZ - calibration_offset.z - (int32_t)auto_tare_z;
            
            // Calculate gradient vector magnitude
            float magnitude = sqrt((float)gradX * gradX + (float)gradY * gradY + (float)gradZ * gradZ);
            
            // Calculate absolute nT value
            float nt_value = (magnitude * 1000.0f) / (MFS_NT_CONVERSION_FACTOR * (float)current_cycle_count);
            
            float acc[3], gyr[3];
            imu_read(acc, gyr);
            
            // 1. Align IMU coordinates to the physical wand shaft BEFORE sensor fusion
            float angle_rad = cal_config.imu_rotation_deg * (float)M_PI / 180.0f;
            float cosA = cosf(angle_rad);
            float sinA = sinf(angle_rad);
            
            // Align Accelerometer
            float ax_aligned = acc[0] * cosA - acc[2] * sinA;
            float az_aligned = acc[0] * sinA + acc[2] * cosA;
            acc[0] = ax_aligned;
            acc[2] = az_aligned;

            // Align Gyroscope
            float gx_aligned = gyr[0] * cosA - gyr[2] * sinA;
            float gz_aligned = gyr[0] * sinA + gyr[2] * cosA;
            gyr[0] = gx_aligned;
            gyr[2] = gz_aligned;
            
            // 2. Feed aligned data to Madgwick Filter for Quaternion Fusion
            // This ensures the Quaternions represent the WAND in 3D space, not the PCB chip!
            MadgwickAHRSupdateIMU(gyr[0], gyr[1], gyr[2], acc[0], acc[1], acc[2]);
            
            // 3. Extract the Gravity Vector directly from the Quaternions (Buttery Smooth!)
            // Standard AHRS Gravity formula from unit quaternion (q0=w, q1=x, q2=y, q3=z)
            float norm_ax = 2.0f * (q1 * q3 - q0 * q2);
            float norm_ay = 2.0f * (q0 * q1 + q2 * q3);
            float norm_az = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;
            
            // 4. Vector Projection for True Down
            // Dot product of Gradient Vector and the new smooth Gravity Vector
            float true_Z = ((float)gradX * norm_ax) + ((float)gradY * norm_ay) + ((float)gradZ * norm_az);

            bool is_pin = false;
            // Negative threshold for True Z depends on sensor polarity and hemisphere
            if (true_Z < -30.0f || true_Z > 30.0f) { // Will refine polarity threshold later
                is_pin = true;
            }

            // Calculate mathematically isolated horizontal planar gradient for the Radar Dot
            // Subtract the vertical component from the 3D gradient vector
            float Hx = (float)gradX - true_Z * norm_ax;
            float Hy = (float)gradY - true_Z * norm_ay;
            float Hz = (float)gradZ - true_Z * norm_az;
            
            // X-axis is generally Right/Left.
            float right_grad = Hx;
            
            // Forward vector is orthogonal to Gravity (UP) and X-axis (RIGHT). 
            // Forward = UP x RIGHT = (0, norm_az, -norm_ay)
            float mag_F = sqrt(norm_az*norm_az + norm_ay*norm_ay);
            float fwd_grad = 0.0f;
            if (mag_F > 0.01f) {
                fwd_grad = (Hy * norm_az - Hz * norm_ay) / mag_F;
            }

            UIData ui_data;
            ui_data.cal_progress = 0;
            ui_data.mag = magnitude;
            ui_data.nt = nt_value;
            ui_data.gradX = right_grad;
            ui_data.gradY = fwd_grad;
            ui_data.trueZ = true_Z;
            ui_data.is_pin = is_pin;
            ui_data.tare_active = (calibration_offset.x != 0 || calibration_offset.y != 0 || calibration_offset.z != 0);
            ui_data.auto_tare_on = auto_tare_enabled;
            ui_data.battery_voltage = current_battery_voltage;

            xQueueOverwrite(audioQueue, &magnitude);
            
            // Re-calculate the audio target frequency for logging
            float target_freq = 40.0f;
            if (nt_value > MFS_AUDIO_SQUELCH_NT) {
                float gain_multiplier = 0.0666f * expf(0.05416f * current_audio_gain);
                target_freq = 40.0f + ((nt_value - MFS_AUDIO_SQUELCH_NT) * gain_multiplier);
            }
            if (target_freq > 3000.0f) target_freq = 3000.0f;

            log_data(millis(), current_battery_voltage, current_audio_gain, current_cycle_count, ref_raw_x, ref_raw_y, ref_raw_z, tip_raw_x, tip_raw_y, tip_raw_z, ref.x, ref.y, ref.z, tip.x, tip.y, tip.z, calibration_offset.x, calibration_offset.y, calibration_offset.z, gradX, gradY, gradZ, magnitude, nt_value, acc[0], acc[1], acc[2], gyr[0], gyr[1], gyr[2], target_freq, is_muted, q0, q1, q2, q3);

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
            
            // Wait to lower CPU load (cap at ~50Hz max)
            xQueueOverwrite(gradientQueue, &ui_data);
            vTaskDelay(pdMS_TO_TICKS(20)); 
        }
        else {
            // Watchdog: Check for timeout lockup (no DRDY for 500ms)
            if ((xTaskGetTickCount() - last_read_time) > pdMS_TO_TICKS(500)) {
                Serial.println("Sensor lockup detected (DRDY timeout)! Resetting...");
                initRM3100(rm3100_tip_handle);
                initRM3100(rm3100_ref_handle);
                last_read_time = xTaskGetTickCount(); // Reset timeout
            }
            vTaskDelay(pdMS_TO_TICKS(5));
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

void setup() {
    Serial.begin(921600);
    pinMode(0, INPUT_PULLUP); // BOOT button for screenshots
    
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

    Serial.printf("\n--- Magnetic Field Scanner %s ---\n", FIRMWARE_VERSION);

    // Initialize display components first (which initializes hardware I2C buses)
    i2c_master_Init(); // I2C for touch panel and peripherals
    
    // Register RM3100s on the shared hardware I2C bus (user_i2c_port0_handle)
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ADDR_TIP,
        .scl_speed_hz = 100000,
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
