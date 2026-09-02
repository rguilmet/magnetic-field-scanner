#include "SensorFusion.h"
#include <math.h>

SensorFusion::SensorFusion() {
    tare_requested = false;
    auto_tare_enabled = false;
    calibration_offset = {0, 0, 0};
    auto_tare_x = 0.0f;
    auto_tare_y = 0.0f;
    auto_tare_z = 0.0f;
    
    history_idx = 0;
    stable_ticks = 0;
    has_printed_tare = false;

    for (int i=0; i<50; i++) {
        for (int j=0; j<3; j++) {
            acc_history[i][j] = 0.0f;
            gyr_history[i][j] = 0.0f;
        }
    }
}

void SensorFusion::init(CalibrationConfig* config, SystemSettings* settings) {
    _cal_config = config;
    _settings = settings;
}

void SensorFusion::setTareRequested() {
    tare_requested = true;
}

void SensorFusion::resetTare() {
    calibration_offset.x = 0;
    calibration_offset.y = 0;
    calibration_offset.z = 0;
    auto_tare_x = 0.0f;
    auto_tare_y = 0.0f;
    auto_tare_z = 0.0f;
    tare_requested = false;
}

void SensorFusion::setAutoTareEnabled(bool enabled) {
    auto_tare_enabled = enabled;
    if (!enabled) {
        auto_tare_x = 0.0f;
        auto_tare_y = 0.0f;
        auto_tare_z = 0.0f;
    }
}



SensorFusionOutput SensorFusion::processUpdate(Vector3Float& tip, Vector3Float& ref, float acc[3], float gyr[3], uint16_t cycle_count) {
    SensorFusionOutput out = {0};

    float t_x = raw_to_nT(tip.x, cycle_count);
    float t_y = raw_to_nT(tip.y, cycle_count);
    float t_z = raw_to_nT(tip.z, cycle_count);

    float r_x = raw_to_nT(ref.x, cycle_count);
    float r_y = raw_to_nT(ref.y, cycle_count);
    float r_z = raw_to_nT(ref.z, cycle_count);

    // Apply Hard/Soft Iron Calibration to Tip
    float tx = t_x - _cal_config->tip_hard[0];
    float ty = t_y - _cal_config->tip_hard[1];
    float tz = t_z - _cal_config->tip_hard[2];
    
    tip.x = (tx * _cal_config->tip_soft[0][0] + ty * _cal_config->tip_soft[0][1] + tz * _cal_config->tip_soft[0][2]);
    tip.y = (tx * _cal_config->tip_soft[1][0] + ty * _cal_config->tip_soft[1][1] + tz * _cal_config->tip_soft[1][2]);
    tip.z = (tx * _cal_config->tip_soft[2][0] + ty * _cal_config->tip_soft[2][1] + tz * _cal_config->tip_soft[2][2]);

    // Apply Hard/Soft Iron Calibration to Ref
    float rx = r_x - _cal_config->ref_hard[0];
    float ry = r_y - _cal_config->ref_hard[1];
    float rz = r_z - _cal_config->ref_hard[2];

    ref.x = (rx * _cal_config->ref_soft[0][0] + ry * _cal_config->ref_soft[0][1] + rz * _cal_config->ref_soft[0][2]);
    ref.y = (rx * _cal_config->ref_soft[1][0] + ry * _cal_config->ref_soft[1][1] + rz * _cal_config->ref_soft[1][2]);
    ref.z = (rx * _cal_config->ref_soft[2][0] + ry * _cal_config->ref_soft[2][1] + rz * _cal_config->ref_soft[2][2]);

    float raw_gradX = tip.x - ref.x;
    float raw_gradY = tip.y - ref.y;
    float raw_gradZ = tip.z - ref.z;

    if (tare_requested) {
        calibration_offset.x = raw_gradX;
        calibration_offset.y = raw_gradY;
        calibration_offset.z = raw_gradZ;
        auto_tare_x = 0.0f;
        auto_tare_y = 0.0f;
        auto_tare_z = 0.0f;
        tare_requested = false;
        Serial.println("Manual Tare complete! Baseline zeroed.");
        return out; // Calling function must handle this blank return or skip
    }

    if (auto_tare_enabled) {
        float dx = raw_gradX - calibration_offset.x - auto_tare_x;
        float dy = raw_gradY - calibration_offset.y - auto_tare_y;
        float dz = raw_gradZ - calibration_offset.z - auto_tare_z;
        float current_mag = sqrtf(dx*dx + dy*dy + dz*dz);
        
        if (current_mag < MFS_AUTO_TARE_THRESHOLD) {
            float base_x = raw_gradX - calibration_offset.x;
            float base_y = raw_gradY - calibration_offset.y;
            float base_z = raw_gradZ - calibration_offset.z;
            auto_tare_x = (auto_tare_x * MFS_EMA_ALPHA) + (base_x * (1.0f - MFS_EMA_ALPHA));
            auto_tare_y = (auto_tare_y * MFS_EMA_ALPHA) + (base_y * (1.0f - MFS_EMA_ALPHA));
            auto_tare_z = (auto_tare_z * MFS_EMA_ALPHA) + (base_z * (1.0f - MFS_EMA_ALPHA));
        }
    }

    float gradX = raw_gradX - calibration_offset.x - auto_tare_x;
    float gradY = raw_gradY - calibration_offset.y - auto_tare_y;
    float gradZ = raw_gradZ - calibration_offset.z - auto_tare_z;
    
    out.gradX = gradX;
    out.gradY = gradY;
    out.gradZ = gradZ;

    out.magnitude = sqrtf(gradX * gradX + gradY * gradY + gradZ * gradZ);
    out.nt_value = out.magnitude;

    // --- DYNAMIC GYRO AUTO-ZERO (TARE) ---
    acc_history[history_idx][0] = acc[0];
    acc_history[history_idx][1] = acc[1];
    acc_history[history_idx][2] = acc[2];
    gyr_history[history_idx][0] = gyr[0];
    gyr_history[history_idx][1] = gyr[1];
    gyr_history[history_idx][2] = gyr[2];
    history_idx = (history_idx + 1) % 50;
    
    if (cycle_count > 100) { 
        float mx=0, my=0, mz=0;
        for (int i=0; i<50; i++) {
            mx += acc_history[i][0];
            my += acc_history[i][1];
            mz += acc_history[i][2];
        }
        mx /= 50.0f; my /= 50.0f; mz /= 50.0f;
        
        float var = 0;
        for (int i=0; i<50; i++) {
            var += (acc_history[i][0]-mx)*(acc_history[i][0]-mx) + 
                   (acc_history[i][1]-my)*(acc_history[i][1]-my) + 
                   (acc_history[i][2]-mz)*(acc_history[i][2]-mz);
        }
        var /= 50.0f;
        
        if (var < 0.001f) {
            stable_ticks++;
            if (stable_ticks == 50) {
                float g_mx=0, g_my=0, g_mz=0;
                for (int i=0; i<50; i++) {
                    g_mx += gyr_history[i][0];
                    g_my += gyr_history[i][1];
                    g_mz += gyr_history[i][2];
                }
                _cal_config->gyr_offset[0] = g_mx / 50.0f;
                _cal_config->gyr_offset[1] = g_my / 50.0f;
                _cal_config->gyr_offset[2] = g_mz / 50.0f;
                if (!has_printed_tare) {
                    Serial.println("DYNAMIC AUTO-ZERO: Gyro bias updated!");
                    has_printed_tare = true;
                }
                stable_ticks = 40; 
            }
        } else {
            stable_ticks = 0;
            has_printed_tare = false;
        }
    }
    
    float ned_ax = acc[0];
    float ned_ay = acc[1];
    float ned_az = acc[2];
    
    float ned_gx = (gyr[0] - _cal_config->gyr_offset[0]) * (float)M_PI / 180.0f;
    float ned_gy = (gyr[1] - _cal_config->gyr_offset[1]) * (float)M_PI / 180.0f;
    float ned_gz = (gyr[2] - _cal_config->gyr_offset[2]) * (float)M_PI / 180.0f;
    
    if (_cal_config->imu_rotation_deg != 0.0f) {
        float angle_rad = -_cal_config->imu_rotation_deg * (float)M_PI / 180.0f; 
        float cosA = cosf(angle_rad);
        float sinA = sinf(angle_rad);
        
        float temp_ax = ned_ax * cosA + ned_az * sinA;
        float temp_az = -ned_ax * sinA + ned_az * cosA;
        ned_ax = temp_ax;
        ned_az = temp_az;
        
        float temp_gx = ned_gx * cosA + ned_gz * sinA;
        float temp_gz = -ned_gx * sinA + ned_gz * cosA;
        ned_gx = temp_gx;
        ned_gz = temp_gz;
    }
    
    float ned_mx =  (float)ref.y;
    float ned_my = -(float)ref.x;
    float ned_mz = -(float)ref.z;
    
    static uint32_t last_micros = 0;
    uint32_t now_micros = micros();
    float dt = 0.02f; // Default to 50Hz for first frame
    if (last_micros != 0) {
        dt = (float)(now_micros - last_micros) / 1000000.0f;
    }
    last_micros = now_micros;
    
    MadgwickAHRSupdate(ned_gx, ned_gy, ned_gz, ned_ax, ned_ay, ned_az, ned_mx, ned_my, ned_mz, dt);
    
    float norm_ax = 2.0f * (q1 * q3 - q0 * q2);
    float norm_ay = 2.0f * (q0 * q1 + q2 * q3);
    float norm_az = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;
    
    out.true_Z = ((float)gradX * norm_ax) + ((float)gradY * norm_ay) + ((float)gradZ * norm_az);

    out.is_pin = false;
    if (out.true_Z < -30.0f || out.true_Z > 30.0f) { 
        out.is_pin = true;
    }

    float Hx = (float)gradX - out.true_Z * norm_ax;
    float Hy = (float)gradY - out.true_Z * norm_ay;
    float Hz = (float)gradZ - out.true_Z * norm_az;
    
    out.right_grad = Hx;
    
    float mag_F = sqrtf(norm_az*norm_az + norm_ay*norm_ay);
    out.fwd_grad = 0.0f;
    if (mag_F > 0.01f) {
        out.fwd_grad = (Hy * norm_az - Hz * norm_ay) / mag_F;
    }

    float siny_cosp = 2.0f * (q0 * q3 + q1 * q2);
    float cosy_cosp = 1.0f - 2.0f * (q2 * q2 + q3 * q3);
    out.azimuth = atan2f(siny_cosp, cosy_cosp) * 180.0f / M_PI;
    
    float sinp = 2.0f * (q0 * q2 - q3 * q1);
    if (sinp > 1.0f) sinp = 1.0f;
    if (sinp < -1.0f) sinp = -1.0f;
    out.elevation = asinf(sinp) * 180.0f / M_PI;
    
    out.azimuth += _settings->mag_declination_deg;
    if (out.azimuth < 0.0f) out.azimuth += 360.0f;
    if (out.azimuth >= 360.0f) out.azimuth -= 360.0f;
    
    return out;
}
