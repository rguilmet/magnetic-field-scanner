#ifndef MATRIX_MATH_H
#define MATRIX_MATH_H

#include <math.h>
#include <string.h>

// Simple 3x3 matrix multiplication: C = A * B
inline void mat_mult_3x3(float A[3][3], float B[3][3], float C[3][3]) {
    for (int i=0; i<3; i++) {
        for (int j=0; j<3; j++) {
            C[i][j] = 0;
            for (int k=0; k<3; k++) C[i][j] += A[i][k] * B[k][j];
        }
    }
}

// Simple 3x3 matrix transpose
inline void mat_trans_3x3(float A[3][3], float At[3][3]) {
    for (int i=0; i<3; i++) {
        for (int j=0; j<3; j++) {
            At[j][i] = A[i][j];
        }
    }
}

// Invert 3x3 matrix
inline bool invert_3x3(float m[3][3], float invOut[3][3]) {
    float det = m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) -
                m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (fabs(det) < 1e-6) return false;
    float invdet = 1.0f / det;
    invOut[0][0] = (m[1][1] * m[2][2] - m[2][1] * m[1][2]) * invdet;
    invOut[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * invdet;
    invOut[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * invdet;
    invOut[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * invdet;
    invOut[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * invdet;
    invOut[1][2] = (m[1][0] * m[0][2] - m[0][0] * m[1][2]) * invdet;
    invOut[2][0] = (m[1][0] * m[2][1] - m[2][0] * m[1][1]) * invdet;
    invOut[2][1] = (m[2][0] * m[0][1] - m[0][0] * m[2][1]) * invdet;
    invOut[2][2] = (m[0][0] * m[1][1] - m[1][0] * m[0][1]) * invdet;
    return true;
}

// Gaussian elimination for 9x9 system: Ax = b
inline bool solve_9x9(double A[9][9], double b[9], double x[9]) {
    double M[9][10];
    for(int i=0; i<9; i++) {
        for(int j=0; j<9; j++) M[i][j] = A[i][j];
        M[i][9] = b[i];
    }
    
    for (int i=0; i<9; i++) {
        // Pivot
        int max_row = i;
        for (int k=i+1; k<9; k++) {
            if (fabs(M[k][i]) > fabs(M[max_row][i])) max_row = k;
        }
        if (fabs(M[max_row][i]) < 1e-12) return false; // Singular
        
        // Swap
        for (int k=i; k<10; k++) {
            double tmp = M[max_row][k];
            M[max_row][k] = M[i][k];
            M[i][k] = tmp;
        }
        
        // Eliminate
        for (int k=i+1; k<9; k++) {
            double c = -M[k][i] / M[i][i];
            for (int j=i; j<10; j++) {
                if (i == j) M[k][j] = 0;
                else M[k][j] += c * M[i][j];
            }
        }
    }
    
    // Back substitution
    for (int i=8; i>=0; i--) {
        x[i] = M[i][9];
        for (int k=i+1; k<9; k++) {
            x[i] -= M[i][k] * x[k];
        }
        x[i] = x[i] / M[i][i];
    }
    return true;
}

// Jacobi eigenvalue algorithm for 3x3 symmetric matrix
inline void jacobi_eigen_3x3(float A[3][3], float V[3][3], float d[3]) {
    for(int i=0; i<3; i++) {
        for(int j=0; j<3; j++) V[i][j] = (i==j)?1.0f:0.0f;
    }
    float B[3][3];
    for(int i=0; i<3; i++) for(int j=0; j<3; j++) B[i][j] = A[i][j];
    
    for(int step=0; step<50; step++) {
        // Find largest off-diagonal element
        int p=0, q=1;
        if(fabs(B[0][2]) > fabs(B[0][1])) { p=0; q=2; }
        if(fabs(B[1][2]) > fabs(B[p][q])) { p=1; q=2; }
        
        if(fabs(B[p][q]) < 1e-6) break;
        
        float theta = 0.5f * atan2(2.0f * B[p][q], B[q][q] - B[p][p]);
        float c = cos(theta);
        float s = sin(theta);
        
        float tempB_pp = c*c*B[p][p] - 2*s*c*B[p][q] + s*s*B[q][q];
        float tempB_qq = s*s*B[p][p] + 2*s*c*B[p][q] + c*c*B[q][q];
        
        B[p][q] = B[q][p] = 0.0f;
        B[p][p] = tempB_pp;
        B[q][q] = tempB_qq;
        
        int r = 3 - p - q;
        float tempB_pr = c*B[p][r] - s*B[q][r];
        float tempB_qr = s*B[p][r] + c*B[q][r];
        B[p][r] = B[r][p] = tempB_pr;
        B[q][r] = B[r][q] = tempB_qr;
        
        for(int i=0; i<3; i++) {
            float tempV_ip = c*V[i][p] - s*V[i][q];
            float tempV_iq = s*V[i][p] + c*V[i][q];
            V[i][p] = tempV_ip;
            V[i][q] = tempV_iq;
        }
    }
    for(int i=0; i<3; i++) d[i] = B[i][i];
}

// Extractor function
inline bool get_calibration_matrices(int32_t *x, int32_t *y, int32_t *z, int count, float center[3], float soft_iron[3][3]) {
    if (count < 10) return false;

    // 1. Calculate Mean and StdDev for normalization
    double mx=0, my=0, mz=0;
    for(int i=0; i<count; i++) {
        mx += x[i]; my += y[i]; mz += z[i];
    }
    mx /= count; my /= count; mz /= count;
    
    double vx=0, vy=0, vz=0;
    for(int i=0; i<count; i++) {
        vx += (x[i] - mx)*(x[i] - mx);
        vy += (y[i] - my)*(y[i] - my);
        vz += (z[i] - mz)*(z[i] - mz);
    }
    double sx = sqrt(vx/count);
    double sy = sqrt(vy/count);
    double sz = sqrt(vz/count);
    
    if (sx < 1 || sy < 1 || sz < 1) return false;

    // 2. Accumulate D^T D and D^T 1 (Normal Equations for Least Squares)
    double DtD[9][9] = {0};
    double Dt1[9] = {0};
    
    for(int i=0; i<count; i++) {
        double nx = (x[i] - mx) / sx;
        double ny = (y[i] - my) / sy;
        double nz = (z[i] - mz) / sz;
        
        double row[9] = {nx*nx, ny*ny, nz*nz, 2*nx*ny, 2*nx*nz, 2*ny*nz, 2*nx, 2*ny, 2*nz};
        for(int r=0; r<9; r++) {
            Dt1[r] += row[r];
            for(int c=0; c<9; c++) {
                DtD[r][c] += row[r] * row[c];
            }
        }
    }
    
    // Tikhonov Regularization (Ridge Regression) to ensure DtD is strictly invertible
    for(int i=0; i<9; i++) {
        DtD[i][i] += 1e-4;
    }
    
    double v[9];
    if (!solve_9x9(DtD, Dt1, v)) return false;
    
    // Form algebraic matrix
    float A[3][3] = {
        {(float)v[0], (float)v[3], (float)v[4]},
        {(float)v[3], (float)v[1], (float)v[5]},
        {(float)v[4], (float)v[5], (float)v[2]}
    };
    float b[3] = {(float)v[6], (float)v[7], (float)v[8]};
    
    float A_inv[3][3];
    if (!invert_3x3(A, A_inv)) return false;
    
    // Normalized center
    float n_center[3];
    for(int i=0; i<3; i++) {
        n_center[i] = 0;
        for(int j=0; j<3; j++) n_center[i] += -A_inv[i][j] * b[j];
    }
    
    // Calculate R matrix for soft iron
    float offset = 0;
    for(int i=0; i<3; i++) offset += n_center[i] * b[i];
    float denom = offset - (-1.0f);
    
    float R[3][3];
    for(int i=0; i<3; i++) {
        for(int j=0; j<3; j++) R[i][j] = A[i][j] / denom;
    }
    
    float V[3][3], evals[3];
    jacobi_eigen_3x3(R, V, evals);
    
    float radii[3];
    float sum_radii = 0;
    for(int i=0; i<3; i++) {
        radii[i] = 1.0f / sqrt(fabs(evals[i]));
        sum_radii += radii[i];
    }
    float target_radius = sum_radii / 3.0f;
    
    float D_diag[3][3] = {0};
    for(int i=0; i<3; i++) D_diag[i][i] = target_radius / radii[i];
    
    // nW = V * D_diag * V^T
    float temp[3][3], Vt[3][3], nW[3][3];
    mat_trans_3x3(V, Vt);
    mat_mult_3x3(V, D_diag, temp);
    mat_mult_3x3(temp, Vt, nW);
    
    // Un-normalize W by scaling columns by 1/std
    float S_inv[3][3] = {
        {1.0f/(float)sx, 0, 0},
        {0, 1.0f/(float)sy, 0},
        {0, 0, 1.0f/(float)sz}
    };
    float W_raw[3][3];
    mat_mult_3x3(nW, S_inv, W_raw);
    
    // Rescale to preserve approximate physical magnitude
    // Quick approximation: average scaling factor
    float scale = (W_raw[0][0] + W_raw[1][1] + W_raw[2][2]) / 3.0f;
    for(int i=0; i<3; i++) {
        for(int j=0; j<3; j++) soft_iron[i][j] = W_raw[i][j] / scale;
    }
    
    // Final center in physical units
    center[0] = n_center[0] * sx + mx;
    center[1] = n_center[1] * sy + my;
    center[2] = n_center[2] * sz + mz;
    
    return true;
}

// Kabsch alignment to find Rotation matrix R that aligns Ref onto Tip
inline bool kabsch_align_3x3(int32_t *ref_x, int32_t *ref_y, int32_t *ref_z,
                             int32_t *tip_x, int32_t *tip_y, int32_t *tip_z,
                             int count, 
                             float ref_center[3], float ref_soft[3][3],
                             float tip_center[3], float tip_soft[3][3],
                             float R_out[3][3]) {
    // Calculate covariance matrix H = P^T * Q on the fly
    double H[3][3] = {0};
    for (int i=0; i<count; i++) {
        // P: Calibrated Ref
        float rx = (float)ref_x[i] - ref_center[0];
        float ry = (float)ref_y[i] - ref_center[1];
        float rz = (float)ref_z[i] - ref_center[2];
        float px = (rx * ref_soft[0][0] + ry * ref_soft[0][1] + rz * ref_soft[0][2]) / 5000.0f;
        float py = (rx * ref_soft[1][0] + ry * ref_soft[1][1] + rz * ref_soft[1][2]) / 5000.0f;
        float pz = (rx * ref_soft[2][0] + ry * ref_soft[2][1] + rz * ref_soft[2][2]) / 5000.0f;
        
        // Q: Calibrated Tip
        float tx = (float)tip_x[i] - tip_center[0];
        float ty = (float)tip_y[i] - tip_center[1];
        float tz = (float)tip_z[i] - tip_center[2];
        float qx = (tx * tip_soft[0][0] + ty * tip_soft[0][1] + tz * tip_soft[0][2]) / 5000.0f;
        float qy = (tx * tip_soft[1][0] + ty * tip_soft[1][1] + tz * tip_soft[1][2]) / 5000.0f;
        float qz = (tx * tip_soft[2][0] + ty * tip_soft[2][1] + tz * tip_soft[2][2]) / 5000.0f;
        
        H[0][0] += px * qx; H[0][1] += px * qy; H[0][2] += px * qz;
        H[1][0] += py * qx; H[1][1] += py * qy; H[1][2] += py * qz;
        H[2][0] += pz * qx; H[2][1] += pz * qy; H[2][2] += pz * qz;
    }
    
    float Hf[3][3];
    for(int i=0; i<3; i++) for(int j=0; j<3; j++) Hf[i][j] = (float)H[i][j];
    
    // SVD of Hf: H^T * H = V * S2 * V^T
    float Ht[3][3], HtH[3][3];
    mat_trans_3x3(Hf, Ht);
    mat_mult_3x3(Ht, Hf, HtH);
    
    float V[3][3], S2[3];
    jacobi_eigen_3x3(HtH, V, S2);
    
    float S_inv[3][3] = {0};
    for(int i=0; i<3; i++) {
        if (S2[i] > 1e-6) S_inv[i][i] = 1.0f / sqrt(S2[i]);
    }
    
    // U = H * V * S^-1
    float temp[3][3], U[3][3];
    mat_mult_3x3(Hf, V, temp);
    mat_mult_3x3(temp, S_inv, U);
    
    // R = V * U^T
    float Ut[3][3];
    mat_trans_3x3(U, Ut);
    mat_mult_3x3(V, Ut, R_out);
    
    // Check reflection: det(R) < 0
    float det = R_out[0][0] * (R_out[1][1] * R_out[2][2] - R_out[2][1] * R_out[1][2]) -
                R_out[0][1] * (R_out[1][0] * R_out[2][2] - R_out[1][2] * R_out[2][0]) +
                R_out[0][2] * (R_out[1][0] * R_out[2][1] - R_out[1][1] * R_out[2][0]);
    if (det < 0) {
        for(int i=0; i<3; i++) V[i][2] *= -1;
        mat_mult_3x3(V, Ut, R_out);
    }
    
    return true;
}

#endif // MATRIX_MATH_H
