
#pragma once
#include <Arduino.h>
#include "src/settings_manager/settings_manager.h"
#include "src/imu_bsp/MadgwickAHRS.h"
#include "user_config.h"

struct Vector3Int {
    int32_t x;
    int32_t y;
    int32_t z;
};

struct Vector3Float {
    float x;
    float y;
    float z;
};

inline float raw_to_nT(int32_t raw, uint16_t cc) {
    return (raw * 1000.0f) / (MFS_NT_CONVERSION_FACTOR * (float)cc);
}

struct SensorFusionOutput {
    float magnitude;
    float nt_value;
    float true_Z;
    float right_grad;
    float fwd_grad;
    float azimuth;
    float elevation;
    bool is_pin;
    
    // For logging:
    float gradX;
    float gradY;
    float gradZ;
};

class SensorFusion {
public:
    SensorFusion();
    void init(CalibrationConfig* config, SystemSettings* settings);

    void setTareRequested();
    void resetTare();
    void setAutoTareEnabled(bool enabled);
    bool isAutoTareEnabled() const { return auto_tare_enabled; }
    bool isTareActive() const { return (calibration_offset.x != 0 || calibration_offset.y != 0 || calibration_offset.z != 0); }
    bool isTareRequested() const { return tare_requested; }
    
    Vector3Float getCalibrationOffset() const { return calibration_offset; }

    SensorFusionOutput processUpdate(Vector3Float& tip, Vector3Float& ref, float acc[3], float gyr[3], uint16_t cycle_count);

private:
    CalibrationConfig* _cal_config;
    SystemSettings* _settings;

    bool tare_requested;
    bool auto_tare_enabled;
    Vector3Float calibration_offset;
    float auto_tare_x;
    float auto_tare_y;
    float auto_tare_z;

    // Dynamic Gyro FOC buffers
    float acc_history[50][3];
    float gyr_history[50][3];
    int history_idx;
    int stable_ticks;
    bool has_printed_tare;
};

