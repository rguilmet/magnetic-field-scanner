//=============================================================================================
// MadgwickAHRS.h
//=============================================================================================
#ifndef MadgwickAHRS_h
#define MadgwickAHRS_h
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile float q0, q1, q2, q3;	// quaternion of sensor frame relative to auxiliary frame

void MadgwickAHRSupdate(float gx, float gy, float gz, float ax, float ay, float az, float mx, float my, float mz);
void MadgwickAHRSupdateIMU(float gx, float gy, float gz, float ax, float ay, float az);

#ifdef __cplusplus
}
#endif

#endif