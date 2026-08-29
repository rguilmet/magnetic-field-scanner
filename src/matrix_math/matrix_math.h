#pragma once

#include <math.h>

// Helper to solve 9x9 system (used for ellipsoid fitting)
inline bool solve_9x9(double M[9][10], double b[9], double x[9]) {
    for (int i=0; i<9; i++) {
        M[i][9] = b[i];
    }
    
    // Gaussian elimination with partial pivoting
    for (int i=0; i<9; i++) {
        int max_row = i;
        double max_val = fabs(M[i][i]);
        for (int k=i+1; k<9; k++) {
            if (fabs(M[k][i]) > max_val) {
                max_val = fabs(M[k][i]);
                max_row = k;
            }
        }
        
        if (max_val < 1e-6) {
            M[i][i] += 1e-4; // Add tiny regularization to prevent exact singularity
        }
        
        for (int k=i; k<10; k++) {
            double tmp = M[max_row][k];
            M[max_row][k] = M[i][k];
            M[i][k] = tmp;
        }
        
        for (int k=i+1; k<9; k++) {
            double c = -M[k][i] / M[i][i];
            for (int j=i; j<10; j++) {
                if (i == j) M[k][j] = 0;
                else M[k][j] += c * M[i][j];
            }
        }
    }
    
    for (int i=8; i>=0; i--) {
        x[i] = M[i][9];
        for (int j=i+1; j<9; j++) {
            x[i] -= M[i][j] * x[j];
        }
        x[i] /= M[i][i];
    }
    return true;
}

inline bool invert_3x3(double m[3][3], double inv[3][3]) {
    double det = m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) -
                 m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                 m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

    if (fabs(det) < 1e-6) return false;

    double invdet = 1.0 / det;

    inv[0][0] = (m[1][1] * m[2][2] - m[2][1] * m[1][2]) * invdet;
    inv[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * invdet;
    inv[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * invdet;
    inv[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * invdet;
    inv[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * invdet;
    inv[1][2] = (m[1][0] * m[0][2] - m[0][0] * m[1][2]) * invdet;
    inv[2][0] = (m[1][0] * m[2][1] - m[2][0] * m[1][1]) * invdet;
    inv[2][1] = (m[2][0] * m[0][1] - m[0][0] * m[2][1]) * invdet;
    inv[2][2] = (m[0][0] * m[1][1] - m[1][0] * m[0][1]) * invdet;
    return true;
}

inline void mat_mult_3x3(double A[3][3], double B[3][3], double C[3][3]) {
    for(int i=0; i<3; i++) {
        for(int j=0; j<3; j++) {
            C[i][j] = 0;
            for(int k=0; k<3; k++) C[i][j] += A[i][k] * B[k][j];
        }
    }
}

inline void mat_mult_3x3_float(float A[3][3], float B[3][3], float C[3][3]) {
    for(int i=0; i<3; i++) {
        for(int j=0; j<3; j++) {
            C[i][j] = 0.0f;
            for(int k=0; k<3; k++) C[i][j] += A[i][k] * B[k][j];
        }
    }
}

inline void mat_trans_3x3(double A[3][3], double B[3][3]) {
    for(int i=0; i<3; i++) {
        for(int j=0; j<3; j++) B[i][j] = A[j][i];
    }
}

// Jacobi eigenvalue algorithm for 3x3 symmetric matrix
inline void jacobi_eigen_3x3(double A[3][3], double V[3][3], double d[3]) {
    for(int i=0; i<3; i++) {
        for(int j=0; j<3; j++) V[i][j] = (i==j)?1.0:0.0;
    }
    double B[3][3];
    for(int i=0; i<3; i++) for(int j=0; j<3; j++) B[i][j] = A[i][j];
    
    for(int step=0; step<50; step++) {
        int p=0, q=1;
        double max_val = fabs(B[0][1]);
        if(fabs(B[0][2]) > max_val) { max_val = fabs(B[0][2]); p=0; q=2; }
        if(fabs(B[1][2]) > max_val) { max_val = fabs(B[1][2]); p=1; q=2; }
        
        if(fabs(B[p][q]) < 1e-6) break;
        
        double theta = 0.5 * atan2(2.0 * B[p][q], B[q][q] - B[p][p]);
        double c = cos(theta);
        double s = sin(theta);
        
        double tempB_pp = c*c*B[p][p] - 2*s*c*B[p][q] + s*s*B[q][q];
        double tempB_qq = s*s*B[p][p] + 2*s*c*B[p][q] + c*c*B[q][q];
        
        B[p][q] = B[q][p] = 0.0;
        B[p][p] = tempB_pp;
        B[q][q] = tempB_qq;
        
        int r = 3 - p - q;
        double tempB_pr = c*B[p][r] - s*B[q][r];
        double tempB_qr = s*B[p][r] + c*B[q][r];
        B[p][r] = B[r][p] = tempB_pr;
        B[q][r] = B[r][q] = tempB_qr;
        
        for(int i=0; i<3; i++) {
            double tempV_ip = c*V[i][p] - s*V[i][q];
            double tempV_iq = s*V[i][p] + c*V[i][q];
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

    // 2. Accumulate D^T D and D^T 1
    double DtD[9][10] = {0}; // Extra column for 'b' in solve_9x9
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
    
    for (int i=0; i<9; i++) {
        DtD[i][i] += 1e-4; // Regularization
    }
    
    double v[9];
    if (!solve_9x9(DtD, Dt1, v)) return false;
    
    double A[3][3] = {
        {v[0], v[3], v[4]},
        {v[3], v[1], v[5]},
        {v[4], v[5], v[2]}
    };
    double b[3] = {v[6], v[7], v[8]};
    
    double A_inv[3][3];
    if (!invert_3x3(A, A_inv)) return false;
    
    double n_center[3];
    for(int i=0; i<3; i++) {
        n_center[i] = 0;
        for(int j=0; j<3; j++) n_center[i] += -A_inv[i][j] * b[j];
    }
    
    double offset = 0;
    for(int i=0; i<3; i++) offset += n_center[i] * b[i];
    double denom = offset - (-1.0);
    
    double R[3][3];
    for(int i=0; i<3; i++) {
        for(int j=0; j<3; j++) R[i][j] = A[i][j] / denom;
    }
    
    double V[3][3], evals[3];
    jacobi_eigen_3x3(R, V, evals);
    
    double radii[3];
    double sum_radii = 0;
    for(int i=0; i<3; i++) {
        radii[i] = 1.0 / sqrt(fabs(evals[i]));
        sum_radii += radii[i];
    }
    double target_radius = sum_radii / 3.0;
    
    double D_diag[3][3] = {0};
    for(int i=0; i<3; i++) D_diag[i][i] = target_radius / radii[i];
    
    double temp[3][3], Vt[3][3], nW[3][3];
    mat_trans_3x3(V, Vt);
    mat_mult_3x3(V, D_diag, temp);
    mat_mult_3x3(temp, Vt, nW);
    
    double S_inv[3][3] = {
        {1.0/sx, 0, 0},
        {0, 1.0/sy, 0},
        {0, 0, 1.0/sz}
    };
    double W_raw[3][3];
    mat_mult_3x3(nW, S_inv, W_raw);
    
    double scale = (W_raw[0][0] + W_raw[1][1] + W_raw[2][2]) / 3.0;
    for(int i=0; i<3; i++) {
        if(i==0) center[0] = (float)(n_center[0]*sx + mx);
        if(i==1) center[1] = (float)(n_center[1]*sy + my);
        if(i==2) center[2] = (float)(n_center[2]*sz + mz);
        for(int j=0; j<3; j++) soft_iron[i][j] = (float)(W_raw[i][j] / scale);
    }
    
    return true;
}

inline bool kabsch_align_3x3(int32_t *ref_x, int32_t *ref_y, int32_t *ref_z,
                             int32_t *tip_x, int32_t *tip_y, int32_t *tip_z,
                             int count, 
                             float ref_center[3], float ref_soft[3][3],
                             float tip_center[3], float tip_soft[3][3],
                             float R_out[3][3]) {
    double H[3][3] = {0};
    for (int i=0; i<count; i++) {
        double rx = (double)ref_x[i] - ref_center[0];
        double ry = (double)ref_y[i] - ref_center[1];
        double rz = (double)ref_z[i] - ref_center[2];
        double px = (rx * ref_soft[0][0] + ry * ref_soft[0][1] + rz * ref_soft[0][2]) / 5000.0;
        double py = (rx * ref_soft[1][0] + ry * ref_soft[1][1] + rz * ref_soft[1][2]) / 5000.0;
        double pz = (rx * ref_soft[2][0] + ry * ref_soft[2][1] + rz * ref_soft[2][2]) / 5000.0;
        
        double tx = (double)tip_x[i] - tip_center[0];
        double ty = (double)tip_y[i] - tip_center[1];
        double tz = (double)tip_z[i] - tip_center[2];
        double qx = (tx * tip_soft[0][0] + ty * tip_soft[0][1] + tz * tip_soft[0][2]) / 5000.0;
        double qy = (tx * tip_soft[1][0] + ty * tip_soft[1][1] + tz * tip_soft[1][2]) / 5000.0;
        double qz = (tx * tip_soft[2][0] + ty * tip_soft[2][1] + tz * tip_soft[2][2]) / 5000.0;
        
        H[0][0] += px * qx; H[0][1] += px * qy; H[0][2] += px * qz;
        H[1][0] += py * qx; H[1][1] += py * qy; H[1][2] += py * qz;
        H[2][0] += pz * qx; H[2][1] += pz * qy; H[2][2] += pz * qz;
    }
    
    double Ht[3][3], HtH[3][3];
    mat_trans_3x3(H, Ht);
    mat_mult_3x3(Ht, H, HtH);
    
    double V[3][3], S2[3];
    jacobi_eigen_3x3(HtH, V, S2);
    
    double S_inv[3][3] = {0};
    for(int i=0; i<3; i++) {
        if (S2[i] > 1e-6) S_inv[i][i] = 1.0 / sqrt(S2[i]);
    }
    
    double temp[3][3], U[3][3];
    mat_mult_3x3(H, V, temp);
    mat_mult_3x3(temp, S_inv, U);
    
    double Ut[3][3], R[3][3];
    mat_trans_3x3(U, Ut);
    mat_mult_3x3(V, Ut, R);
    
    double det = R[0][0] * (R[1][1] * R[2][2] - R[2][1] * R[1][2]) -
                 R[0][1] * (R[1][0] * R[2][2] - R[1][2] * R[2][0]) +
                 R[0][2] * (R[1][0] * R[2][1] - R[1][1] * R[2][0]);
    
    if (det < 0) {
        for(int i=0; i<3; i++) {
            V[i][2] = -V[i][2];
        }
        mat_mult_3x3(H, V, temp);
        mat_mult_3x3(temp, S_inv, U);
        mat_trans_3x3(U, Ut);
        mat_mult_3x3(V, Ut, R);
    }
    
    for(int i=0; i<3; i++) {
        for(int j=0; j<3; j++) R_out[i][j] = (float)R[i][j];
    }
    
    return true;
}

