#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void imu_init(void);
void imu_read(float *acc, float *gyr, int16_t *temp);

#ifdef __cplusplus
}
#endif
