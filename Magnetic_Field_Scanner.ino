#include <Arduino.h>

// Helper structure to cleanly bundle 3D vector points
struct MagData {
    int32_t x;
    int32_t y;
    int32_t z;
};

#include <math.h>
#include <stdarg.h>
#include "user_config.h"
#include <Field_Instrument_Core.h>
#include <boards/waveshare_s3_349_v3.h>
#include "src/lvgl_port/lvgl_port.h"
#include "src/button_bsp/button_bsp.h"
#include "src/settings_manager/settings_manager.h"
#include "src/data_logger/data_logger.h"
#include "src/sensor_fusion/SensorFusion.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_heap_caps.h"
#include <SD.h>
#include <FFat.h>
#include <SPI.h>

extern SemaphoreHandle_t log_mux;
esp_io_expander_handle_t io_expander = NULL;
bool sd_card_mounted = false;
static bool ffat_mounted = false;
static bool is_vbatpowerflag = false;
static FicBoardProfile_t s_board_cfg;

// Valid Hardwired 7-bit Addresses
#define ADDR_TIP        0x23
#define ADDR_REF        0x21
#define ADDR_NEAR       0x22

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

static int mfs_printf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.print(buf);
    return n;
}

// LEDC Backlight Control (Arduino wrapper for LVGL port)
extern "C" void init_backlight_pwm(void) {
    // ESP32 Arduino Core 3.x API
    ledcAttach(MFS_PIN_NUM_BK_LIGHT, 5000, 8); // Pin 42, 5kHz, 8-bit
    ledcWrite(MFS_PIN_NUM_BK_LIGHT, 128);      // Start at ~50%
}
extern "C" void set_backlight_pwm(uint8_t brightness) {
    ledcWrite(MFS_PIN_NUM_BK_LIGHT, brightness);
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
    if (fic_i2c_lock(250)) {
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
        fic_i2c_unlock();
    }
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

    // 0. Force Idle Mode (Disable CMM) so we can safely update Cycle Counts!
    // If the sensor was left free-running from a previous boot or firmware flash,
    // the datasheet explicitly warns that writing to CC registers will fail or corrupt!
    uint8_t cmm_data[] = {REG_CMM, 0x00};
    fic_i2c_write_buff(handle, -1, cmm_data, sizeof(cmm_data));

    // 1. Set the Cycle Counts (MSB first, then LSB)
    uint8_t msb = (current_cycle_count >> 8) & 0xFF;
    uint8_t lsb = current_cycle_count & 0xFF;

    uint8_t ccx_data[] = {REG_CCX, msb, lsb, msb, lsb, msb, lsb};
    fic_i2c_write_buff(handle, -1, ccx_data, sizeof(ccx_data));

    // Notice: We intentionally do NOT write to TMRC or CMM.
    // The sensor remains entirely idle, waiting for the ESP32 to manually send a POLL command.
}

static void mfs_rm3100_bus0_detach(i2c_master_bus_handle_t bus_handle, void *ctx) {
    (void)bus_handle;
    (void)ctx;
    if (rm3100_tip_handle) { i2c_master_bus_rm_device(rm3100_tip_handle); rm3100_tip_handle = NULL; }
    if (rm3100_ref_handle) { i2c_master_bus_rm_device(rm3100_ref_handle); rm3100_ref_handle = NULL; }
    if (rm3100_mid_handle) { i2c_master_bus_rm_device(rm3100_mid_handle); rm3100_mid_handle = NULL; }
}

static void mfs_rm3100_bus0_reattach(i2c_master_bus_handle_t bus_handle, void *ctx) {
    (void)bus_handle;
    (void)ctx;
    (void)fic_i2c_add_i2c0_device(ADDR_TIP, 200000U, &rm3100_tip_handle);
    (void)fic_i2c_add_i2c0_device(ADDR_REF, 200000U, &rm3100_ref_handle);
    (void)fic_i2c_add_i2c0_device(ADDR_NEAR, 200000U, &rm3100_mid_handle);
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
        if (playback && fic_i2c_lock(250)) {
            esp_codec_dev_set_out_vol(playback, (float)vol);
            fic_i2c_unlock();
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
    if (fic_i2c_read_buff(handle, REG_RESULTS, buffer, 9) == ESP_OK) {
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

volatile bool is_calibrating = false;
volatile int force_audio_tone = 0;
volatile int calibration_point_count = 0;

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

    for (;;) {
        // Handle deferred cycle count updates
        if (pending_cycle_count != 0) {
            uint16_t count = pending_cycle_count;
            pending_cycle_count = 0;
            current_cycle_count = count;

            Serial.printf("Updating cycle count to %d...\n", count);

            // 1. Re-initialize both sensors (Writes CC registers, leaves in IDLE)
            initRM3100(rm3100_tip_handle);
            initRM3100(rm3100_ref_handle);

            // 2. Discard the first 3 measurements to let the sensor's LR oscillator stabilize at the new gain!
            xEventGroupClearBits(drdy_event_group, DRDY_TIP_BIT | DRDY_REF_BIT);
            for (int i = 0; i < 3; i++) {
                uint8_t poll_data[] = {REG_POLL, 0x70}; // Request X, Y, and Z
                fic_i2c_write_buff(rm3100_tip_handle, -1, poll_data, sizeof(poll_data));
                fic_i2c_write_buff(rm3100_ref_handle, -1, poll_data, sizeof(poll_data));

                EventBits_t dummy_bits = xEventGroupWaitBits(drdy_event_group, DRDY_TIP_BIT | DRDY_REF_BIT, pdTRUE, pdTRUE, pdMS_TO_TICKS(1000));
                if ((dummy_bits & (DRDY_TIP_BIT | DRDY_REF_BIT)) == (DRDY_TIP_BIT | DRDY_REF_BIT)) {
                    uint8_t dummy[9];
                    fic_i2c_read_buff(rm3100_tip_handle, REG_RESULTS, dummy, 9);
                    fic_i2c_read_buff(rm3100_ref_handle, REG_RESULTS, dummy, 9);
                }
            }

            Serial.println("Cycle count update complete.");
        }

        if (!is_scanning) {
            vTaskDelay(pdMS_TO_TICKS(100));
            // Keep resetting timeout so watchdog doesn't trip while paused
            xEventGroupClearBits(drdy_event_group, DRDY_TIP_BIT | DRDY_REF_BIT);
            continue;
        }

        // --- SINGLE MEASUREMENT POLLING ORCHESTRATOR ---
        xEventGroupClearBits(drdy_event_group, DRDY_TIP_BIT | DRDY_REF_BIT);

        uint8_t poll_data[] = {REG_POLL, 0x70}; // 0x70 = Measure X, Y, and Z
        fic_i2c_write_buff(rm3100_tip_handle, -1, poll_data, sizeof(poll_data));
        fic_i2c_write_buff(rm3100_ref_handle, -1, poll_data, sizeof(poll_data));

        if (digitalRead(MFS_PIN_RM3100_TIP_DRDY) == HIGH) {
            xEventGroupSetBits(drdy_event_group, DRDY_TIP_BIT);
        }
        if (digitalRead(MFS_PIN_RM3100_REF_DRDY) == HIGH) {
            xEventGroupSetBits(drdy_event_group, DRDY_REF_BIT);
        }

        // True ISR-driven wait for the POLL measurement to finish (max 1000ms timeout for watchdog)
        EventBits_t bits = xEventGroupWaitBits(drdy_event_group, DRDY_TIP_BIT | DRDY_REF_BIT, pdTRUE, pdTRUE, pdMS_TO_TICKS(1000));

        if ((bits & (DRDY_TIP_BIT | DRDY_REF_BIT)) == (DRDY_TIP_BIT | DRDY_REF_BIT)) {
            MagData tip = readSensor(rm3100_tip_handle);
            MagData ref = readSensor(rm3100_ref_handle);

            // Watchdog: Check for stuck sensor or I2C read failure
            if ((tip.x == 0 && tip.y == 0 && tip.z == 0)) {
                consecutive_same_reads++;
            } else {
                consecutive_same_reads = 0;
            }

            // EMI Slew-Rate Filter
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
                continue;
            }

            if (consecutive_same_reads > 10) {
                Serial.println("Sensor lockup detected (stuck data or dead I2C)! Resetting...");

                uint8_t dummy[9];
                fic_i2c_read_buff(rm3100_tip_handle, REG_RESULTS, dummy, 9);
                fic_i2c_read_buff(rm3100_ref_handle, REG_RESULTS, dummy, 9);

                initRM3100(rm3100_tip_handle);
                initRM3100(rm3100_ref_handle);
                consecutive_same_reads = 0;
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            if (tip.x == 0 && tip.y == 0 && tip.z == 0) {
                continue;
            }

            if (abs(tip.x) > MFS_MAX_GLITCH_MAGNITUDE || abs(tip.y) > MFS_MAX_GLITCH_MAGNITUDE || abs(tip.z) > MFS_MAX_GLITCH_MAGNITUDE ||
                abs(ref.x) > MFS_MAX_GLITCH_MAGNITUDE || abs(ref.y) > MFS_MAX_GLITCH_MAGNITUDE || abs(ref.z) > MFS_MAX_GLITCH_MAGNITUDE) {
                continue;
            }

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
            fic_qmi8658_read(raw_acc, raw_gyr, &imu_temp);

            SensorFusionOutput out = sensorFusion.processUpdate(tip_vec, ref_vec, raw_acc, raw_gyr, current_cycle_count);

            if (out.magnitude == 0 && out.nt_value == 0 && out.true_Z == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            float magnitude = out.magnitude;
            float nt_value = out.nt_value;

            UIData ui_data;
            ui_data.cal_progress = 0;

            float raw_gx = (float)(tip_raw_x - ref_raw_x);
            float raw_gy = (float)(tip_raw_y - ref_raw_y);
            float raw_gz = (float)(tip_raw_z - ref_raw_z);
            ui_data.mag = sqrtf(raw_gx * raw_gx + raw_gy * raw_gy + raw_gz * raw_gz);

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

            float target_freq = current_settings.audio_base_freq;
            if (nt_value > current_settings.audio_min_range_nT) {
                float audio_max_range_nT = 500000.0f * expf(-0.062146f * current_audio_gain);
                if (nt_value > audio_max_range_nT) {
                    target_freq = current_settings.audio_max_freq;
                } else {
                    float n_min_cbrt = cbrtf(current_settings.audio_min_range_nT);
                    float n_max_cbrt = cbrtf(audio_max_range_nT);
                    float n_val_cbrt = cbrtf(nt_value);
                    target_freq = current_settings.audio_base_freq +
                                  ((current_settings.audio_max_freq - current_settings.audio_base_freq) *
                                  ((n_val_cbrt - n_min_cbrt) / (n_max_cbrt - n_min_cbrt)));
                }
            }

            log_data(millis(), current_battery_voltage, current_audio_gain, current_cycle_count,
                     ref_raw_x, ref_raw_y, ref_raw_z, tip_raw_x, tip_raw_y, tip_raw_z,
                     ref_vec.x, ref_vec.y, ref_vec.z, tip_vec.x, tip_vec.y, tip_vec.z,
                     calibration_offset.x, calibration_offset.y, calibration_offset.z,
                     out.gradX, out.gradY, out.gradZ, magnitude, nt_value,
                     raw_acc[0], raw_acc[1], raw_acc[2], raw_gyr[0], raw_gyr[1], raw_gyr[2],
                     imu_temp, target_freq, is_muted,
                     sensorFusion.getQ0(), sensorFusion.getQ1(), sensorFusion.getQ2(), sensorFusion.getQ3(),
                     ui_data.azimuth, ui_data.elevation, current_settings.mag_declination_deg,
                     out.true_Z, out.is_pin);

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
                            (void)arg;
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

                    vTaskDelay(pdMS_TO_TICKS(500));
                    force_audio_tone = 0;
                    ui_data.cal_progress = 100;
                }
            }

            xQueueOverwrite(gradientQueue, &ui_data);
        } else {
            Serial.println("Sensor lockup detected (DRDY timeout)! Resetting...");

            uint8_t dummy[9];
            if (digitalRead(MFS_PIN_RM3100_TIP_DRDY) == HIGH) {
                fic_i2c_read_buff(rm3100_tip_handle, REG_RESULTS, dummy, 9);
            }
            if (digitalRead(MFS_PIN_RM3100_REF_DRDY) == HIGH) {
                fic_i2c_read_buff(rm3100_ref_handle, REG_RESULTS, dummy, 9);
            }

            initRM3100(rm3100_tip_handle);
            initRM3100(rm3100_ref_handle);
        }
    }
}

void task_display_update(void *pvParameters) {
    (void) pvParameters;
    UIData ui_data;
    ui_data.cal_progress = 0;

    for (;;) {
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
        if (!playback) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (xQueueReceive(audioQueue, &currentMagnitude, 0) == pdPASS) {
            current_audio_nt = currentMagnitude;
        }

        float target_freq = current_settings.audio_base_freq;

        if (force_audio_tone > 0) {
            target_freq = (float)force_audio_tone;
        } else {
            if (!is_scanning || is_muted) {
                memset(stream_buf, 0, sizeof(stream_buf));
                esp_codec_dev_write(playback, stream_buf, sizeof(stream_buf));
                smoothed_freq = 0.0f;
                continue;
            }

            if (current_audio_nt > current_settings.audio_min_range_nT) {
                float audio_max_range_nT = 500000.0f * expf(-0.062146f * current_audio_gain);
                if (current_audio_nt > audio_max_range_nT) {
                    target_freq = current_settings.audio_max_freq;
                } else {
                    float n_min_cbrt = cbrtf(current_settings.audio_min_range_nT);
                    float n_max_cbrt = cbrtf(audio_max_range_nT);
                    float n_val_cbrt = cbrtf(current_audio_nt);
                    target_freq = current_settings.audio_base_freq +
                                  ((current_settings.audio_max_freq - current_settings.audio_base_freq) *
                                  ((n_val_cbrt - n_min_cbrt) / (n_max_cbrt - n_min_cbrt)));
                }
            }
        }

        if (target_freq > current_settings.audio_max_freq) target_freq = current_settings.audio_max_freq;

        if (smoothed_freq == 0.0f || force_audio_tone > 0) {
            smoothed_freq = target_freq;
        } else {
            smoothed_freq = (0.2f * target_freq) + (0.8f * smoothed_freq);
        }

        float actual_freq = smoothed_freq;
        if (current_settings.audio_waveform == 3 && force_audio_tone == 0) {
            actual_freq = smoothed_freq / 20.0f;
        }

        float phase_increment = 2.0f * (float)M_PI * actual_freq / (float)SAMPLE_RATE;
        static int click_samples_remaining = 0;

        for (int i = 0; i < AUDIO_CHUNK; i++) {
            int16_t sample = 0;

            if (current_settings.audio_waveform == 3 && force_audio_tone == 0) {
                if (click_samples_remaining > 0) {
                    sample = (click_samples_remaining % 2 == 0) ? 32767 : -32767;
                    click_samples_remaining--;
                } else {
                    sample = 0;
                }
            } else {
                if (current_settings.audio_waveform == 0 || (current_settings.audio_waveform == 3 && force_audio_tone > 0)) {
                    sample = (phase < (float)M_PI) ? 8192 : -8192;
                } else if (current_settings.audio_waveform == 1) {
                    float t = phase / (2.0f * (float)M_PI);
                    sample = (int16_t)(24000.0f * (2.0f * fabs(2.0f * t - 1.0f) - 1.0f));
                } else {
                    sample = (int16_t)(32767.0f * sinf(phase));
                }
            }

            stream_buf[i * 2] = sample;
            stream_buf[i * 2 + 1] = sample;

            phase += phase_increment;
            if (phase >= 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
                if (current_settings.audio_waveform == 3 && force_audio_tone == 0) {
                    click_samples_remaining = (int)(SAMPLE_RATE * 0.002f);
                }
            }
        }

        esp_codec_dev_write(playback, stream_buf, sizeof(stream_buf));
    }
}

static void mfs_button_pwr_task(void* arg)
{
    (void) arg;
    for (;;)
    {
        EventBits_t even = xEventGroupWaitBits(pwr_groups, set_bit_all, pdTRUE, pdFALSE, pdMS_TO_TICKS(2 * 1000));
        if (get_bit_button(even, 1)) // Long press
        {
            if (is_vbatpowerflag && io_expander != NULL)
            {
                is_vbatpowerflag = false;
                if (fic_i2c_lock(250)) {
                    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, MFS_EXIO_PIN_SYS_EN, 0));
                    fic_i2c_unlock();
                }
            }
        }
        else if (get_bit_button(even, 2))
        {
            if (!is_vbatpowerflag)
            {
                is_vbatpowerflag = true;
            }
        }
    }
}

void power_Test(void *arg)
{
    (void) arg;
    if (digitalRead(MFS_PIN_NUM_SYS_OUT) == HIGH)
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

static bool mfs_get_wifi_creds_cb(char *ssid_out, size_t ssid_sz, char *pass_out, size_t pass_sz) {
    if (!ssid_out || !pass_out || ssid_sz == 0 || pass_sz == 0) {
        return false;
    }
    snprintf(ssid_out, ssid_sz, "%s", current_settings.wifi_ssid);
    snprintf(pass_out, pass_sz, "%s", current_settings.wifi_password);
    return true;
}

static void mfs_web_file_uploaded_cb(const char *filename, const char *fs_key) {
    (void)fs_key;
    if (filename == NULL) return;
    if (strstr(filename, "calibration.json") != NULL) {
        load_calibration();
        Serial.println("[PORTAL] Reloaded calibration.json after upload");
    } else if (strstr(filename, "settings.json") != NULL) {
        load_settings();
        Serial.println("[PORTAL] Reloaded settings.json after upload");
    }
}

static int16_t mfs_rtc_utc_offset_cb(void) {
    return current_settings.minutesOffsetToUTC;
}

static void mfs_screenshot_timestamp_cb(char *buf, size_t max_len) {
    get_formatted_timestamp(buf, max_len, false);
}

static void take_screenshot_to_sd(void) {
    FicScreenshotCfg_t sc_cfg = {};
    sc_cfg.primary_fs    = sd_card_mounted ? (fs::FS *)&SD : (fs::FS *)&FFat;
    sc_cfg.primary_name  = sd_card_mounted ? "SD" : "FFat";
    sc_cfg.fallback_fs   = (sd_card_mounted && ffat_mounted) ? (fs::FS *)&FFat : nullptr;
    sc_cfg.fallback_name = "FFat";
    sc_cfg.fs_mux        = log_mux;
    sc_cfg.dir_path      = "/screenshots";
    sc_cfg.width         = MFS_LCD_H_RES;
    sc_cfg.height        = MFS_LCD_V_RES;
    sc_cfg.timestamp_cb  = mfs_screenshot_timestamp_cb;
    sc_cfg.free_kb_cb    = nullptr;
    sc_cfg.log_cb        = mfs_printf;
    char saved_path[80]  = {0};
    fic_screenshot_save_bmp(&sc_cfg, saved_path, sizeof(saved_path));
}

void setup() {
    Serial.begin(921600);
    Serial.setTxTimeoutMs(0);
    pinMode(0, INPUT_PULLUP); // BOOT button for screenshots

    // =========================================================================
    // CRITICAL: BATTERY LATCH INITIALIZATION
    // Assert SYS_EN high IMMEDIATELY on boot so the user does not have to hold
    // the power button for the entire 3000ms Serial wait below!
    // =========================================================================
    s_board_cfg = fic_board_waveshare_s3_349_v3();
    fic_i2c_set_log_callback(mfs_printf);
    (void)fic_i2c_init_from_board(&s_board_cfg);

    if (fic_i2c0_bus != NULL) {
        esp_err_t exio_err = ESP_FAIL;
        for (int attempt = 1; attempt <= 5; attempt++) {
            exio_err = esp_io_expander_new_i2c_tca9554(fic_i2c0_bus, s_board_cfg.display.tca9554_addr, &io_expander);
            if (exio_err == ESP_OK && io_expander != NULL) {
                break;
            }
            fic_i2c_reinit_bus();
            delay(20);
        }
        if (io_expander != NULL) {
            ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, MFS_EXIO_PIN_SYS_EN, IO_EXPANDER_OUTPUT));
            ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, MFS_EXIO_PIN_SYS_EN, 1)); // Latch battery power ON!
            fic_lvgl_set_io_expander(io_expander);
        }
    }

    // Wait for native USB Serial to connect, with a 3 second timeout
    // so the wand still boots instantly on battery power without a PC!
    unsigned long start_time = millis();
    while (!Serial && (millis() - start_time) < 3000) {
        delay(10);
    }
    if (Serial) {
        delay(500);
    }

    pinMode(MFS_PIN_RM3100_TIP_DRDY, INPUT);
    pinMode(MFS_PIN_RM3100_REF_DRDY, INPUT);
    pinMode(MFS_PIN_RM3100_NEAR_DRDY, INPUT);

    drdy_event_group = xEventGroupCreate();
    attachInterrupt(digitalPinToInterrupt(MFS_PIN_RM3100_TIP_DRDY), drdy_tip_isr, RISING);
    attachInterrupt(digitalPinToInterrupt(MFS_PIN_RM3100_REF_DRDY), drdy_ref_isr, RISING);

    Serial.printf("\n--- Magnetic Field Scanner %s ---\n", MFS_FIRMWARE_VERSION);
    Serial.printf("[I2C0 SCAN] TCA9554(0x20):%s | RTC(0x51):%s | IMU(0x6B):%s | TIP(0x23):%s | REF(0x21):%s\n",
                  fic_i2c_probe_addr(0x20) ? "OK" : "--",
                  fic_i2c_probe_addr(0x51) ? "OK" : "--",
                  fic_i2c_probe_addr(0x6B) ? "OK" : "--",
                  fic_i2c_probe_addr(ADDR_TIP) ? "OK" : "--",
                  fic_i2c_probe_addr(ADDR_REF) ? "OK" : "--");

    // Register RM3100s on shared I2C0 hardware bus + runtime bus0 recovery hooks
    (void)fic_i2c_add_i2c0_device(ADDR_TIP, 200000U, &rm3100_tip_handle);
    (void)fic_i2c_add_i2c0_device(ADDR_REF, 200000U, &rm3100_ref_handle);
    (void)fic_i2c_add_i2c0_device(ADDR_NEAR, 200000U, &rm3100_mid_handle);
    (void)fic_i2c_register_bus_hooks(mfs_rm3100_bus0_detach, mfs_rm3100_bus0_reattach, nullptr);

    fic_rtc_set_log_callback(mfs_printf);
    fic_rtc_set_utc_offset_callback(mfs_rtc_utc_offset_cb);
    (void)fic_rtc_init();
    fic_qmi8658_set_log_callback(mfs_printf);
    fic_qmi8658_init();

    // Storage initialization (Internal FFat fallback + SPI SD Card primary)
    if (!FFat.begin(true)) {
        Serial.println("FFat Mount Failed");
    } else {
        ffat_mounted = true;
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

    {
        FicStorageHandle_t ffat_h = {
            &FFat,
            "Internal Flash (FFat)",
            "/ffat",
            log_mux,
            false,
            false
        };
        fic_fs_register_fallback(&ffat_h);
        if (sd_card_mounted) {
            FicStorageHandle_t sd_h = {
                &SD,
                "MicroSD Card",
                "/sd",
                log_mux,
                true,
                true
            };
            fic_fs_register_primary(&sd_h);
        }
        FicWebPortalCfg_t wcfg = {};
        wcfg.portal_title        = "MFS Wand Portal";
        wcfg.version_str         = MFS_FIRMWARE_VERSION;
        wcfg.ap_ssid             = "MFS_Wand_AP";
        wcfg.ap_password         = "";
        wcfg.ntp_server          = "pool.ntp.org";
        wcfg.http_port           = 80U;
        wcfg.primary_fs          = sd_card_mounted ? (fs::FS *)&SD : nullptr;
        wcfg.primary_label       = "MicroSD Card";
        wcfg.primary_vfs_path    = "/sd";
        wcfg.fallback_fs         = &FFat;
        wcfg.fallback_label      = "Internal Flash (FFat)";
        wcfg.fallback_vfs_path   = "/ffat";
        wcfg.fs_mux              = log_mux;
        wcfg.mirror_filename     = "calibration.json";
        wcfg.get_wifi_creds_cb   = mfs_get_wifi_creds_cb;
        wcfg.on_status_cb        = update_wifi_ip_label;
        wcfg.on_set_time_cb      = fic_rtc_set_time;
        wcfg.on_file_uploaded_cb = mfs_web_file_uploaded_cb;
        wcfg.log_cb              = mfs_printf;
        fic_web_portal_configure(&wcfg);
    }

    FicLvglDisplayCfg_t disp_cfg;
    fic_lvgl_cfg_from_board(&s_board_cfg, &disp_cfg);
    disp_cfg.task_stack_size   = LVGL_TASK_STACK_SIZE;
    disp_cfg.task_priority     = FIC_PRIO_LVGL;
    disp_cfg.init_backlight_cb = init_backlight_pwm;
    disp_cfg.create_ui_cb      = create_detector_ui;
    fic_lvgl_port_init(&disp_cfg);

    fic_web_portal_start(); // Auto-start Wi-Fi on boot

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

    // Power tasks (Core 1 UI & power tier, matching unified FIC priority contract)
    xTaskCreatePinnedToCore(mfs_button_pwr_task, "mfs_button_pwr_task", 4 * 1024, NULL, FIC_PRIO_UI_FEED, NULL, FIC_CORE_UI);
    xTaskCreatePinnedToCore(power_Test, "power_Test", 4 * 1024, NULL, FIC_PRIO_UI_FEED, NULL, FIC_CORE_UI);

    Serial.println("Configuring Wand Sensors on Hardware I2C Bus...");

    initRM3100(rm3100_tip_handle);
    initRM3100(rm3100_ref_handle);
    Serial.println("Both sensors ready. Scanning armed.");

    gradientQueue = xQueueCreate(1, sizeof(UIData));
    audioQueue = xQueueCreate(1, sizeof(float));
    if (gradientQueue == NULL || audioQueue == NULL) {
        Serial.println("Failed to create queues");
    }

    Serial.printf("[MEM] Post-Setup Heap -> Internal Free: %u B (Largest: %u B, DMA: %u B) | PSRAM Free: %u KB\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U));

    xTaskCreatePinnedToCore(task_sensor_read, "SensorTask", 4096, NULL, FIC_PRIO_UI_FEED, NULL, FIC_CORE_ACQ);
    xTaskCreatePinnedToCore(task_audio_alert, "AudioTask", 4096, NULL, FIC_PRIO_AUDIO, NULL, FIC_CORE_UI);
    xTaskCreatePinnedToCore(task_display_update, "DisplayTask", 8192, NULL, FIC_PRIO_UI_FEED, NULL, FIC_CORE_UI);
    xTaskCreatePinnedToCore(task_battery_monitor, "BatteryTask", 2048, NULL, FIC_PRIO_BACKGROUND, NULL, FIC_CORE_UI);
}

void loop() {
    fic_web_portal_handle();

    // Debounced 5s settings persistence strictly on Priority-1 background loop() (never on UI tasks)
    if (settings_dirty && ((int32_t)(millis() - last_settings_change_ms) > 5000)) {
        settings_dirty = false;
        save_settings();
    }

    // BOOT Button Screenshot Trigger
    static uint32_t btn_press_start = 0;
    static bool btn_is_pressed = false;

    if (digitalRead(0) == LOW) {
        if (!btn_is_pressed) {
            btn_is_pressed = true;
            btn_press_start = millis();
        } else if (millis() - btn_press_start > 1500) {
            // Long Press Detected (1.5 seconds)
            force_audio_tone = 2000;
            vTaskDelay(pdMS_TO_TICKS(50));
            force_audio_tone = 0;
            vTaskDelay(pdMS_TO_TICKS(50));
            force_audio_tone = 2500;
            vTaskDelay(pdMS_TO_TICKS(100));
            force_audio_tone = 0;

            take_screenshot_to_sd();

            btn_press_start = millis() + 5000;
        }
    } else {
        btn_is_pressed = false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}
