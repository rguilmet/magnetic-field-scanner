
#pragma once
#include <Arduino.h>
#include "settings_manager/settings_manager.h"
#include "imu_bsp/MadgwickAHRS.h"

#define MFS_NT_CONVERSION_FACTOR 0.03671f
#define MFS_EMA_ALPHA 0.999f
#define MFS_AUTO_TARE_THRESHOLD 50.0f

struct Vector3Int {
    int32_t x;
    int32_t y;
    int32_t z;
};

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
    int32_t gradX;
    int32_t gradY;
    int32_t gradZ;
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
    
    Vector3Int getCalibrationOffset() const { return calibration_offset; }

    SensorFusionOutput processUpdate(Vector3Int& tip, Vector3Int& ref, float acc[3], float gyr[3], uint16_t cycle_count);

private:
    CalibrationConfig* _cal_config;
    SystemSettings* _settings;

    bool tare_requested;
    bool auto_tare_enabled;
    Vector3Int calibration_offset;
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

