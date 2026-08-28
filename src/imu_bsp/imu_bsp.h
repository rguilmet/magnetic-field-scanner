#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void imu_init(void);
void imu_read(float *acc, float *gyr);

#ifdef __cplusplus
}
#endif
