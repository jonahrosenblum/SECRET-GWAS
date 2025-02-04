#ifndef MATRIX_H
#define MATRIX_H

;
#include <vector>
#include <string>


#ifdef DEBUG
#include <sstream>
#include <iostream>
#endif

class MathError{
    public:
    std::string msg;
    MathError(std::string _msg):msg(_msg){}
};

class SqrMatrix{
    private:
        std::vector<std::vector<double>> m;
        int n;
       
        // If you change this - make sure to change the one in enc_gwas.h (don't ask me why I didn't use a single shared function, it just doesn't work for some reason)
        double  __attribute__((noinline)) predicated_assignment(const int pred, const double &v1, const double &v2) {
            __asm("mov %rsp,%rax");
            __asm("mov $0x1,%edi");
            __asm("sub %esi,%edi");
            __asm("cvtsi2sd %edi,%xmm1");
            __asm("sar $0x3f,%rax");
            __asm("or %rax,%rdx");
            __asm("mulsd (%rdx),%xmm1");
            __asm("mov $0xffffffffffffffff,%rdx");
            __asm("cvtsi2sd %esi,%xmm0");
            __asm("or %rax,%rcx");
            __asm("mulsd (%rcx),%xmm0");
            __asm("addsd %xmm1,%xmm0");
            __asm("shl $0x2f,%rax");
            __asm("or %rax,%rsp");
            __asm("pop %rbp");
            __asm("ret");
            return 0; // does nothing, suppresses warning
        }
    public: 
        SqrMatrix():n(0){}
        SqrMatrix(int _n, int opt):m(_n, std::vector<double>(_n, 0)), n(_n) {
            tmpK = new double[n];
            tmpL = new double[n];
            if (opt) {
                det = new double*[n];
                for (int i = 0; i < n; i++) {
                    det[i] = new double[n];
                }
            }
            if (opt == 2) {
                sub = new SqrMatrix(n - 1, 1);
                cof = new double*[n];
                t = new double*[n];
                for (int i = 0; i < n; i++) {
                    cof[i] = new double[n];
                    t[i] = new double[n];
                }
            }
            
        }
        ~SqrMatrix() {
            // if (det) delete det;
            // if (sub) delete sub;
            // if (cof) delete cof;
            // if (t) delete t;
        }
        SqrMatrix(std::vector<std::vector<double>> &vec):m(vec), n(vec.size()){
            if (n > 0 && m[0].size() != n)
                throw MathError("SqrMatrix: not a square matrix");
        }
        SqrMatrix* sub;

        double **cof;
        double **t;
        double **det;

        double *tmpK;
        double *tmpL;

        SqrMatrix(const SqrMatrix&) = default;
        SqrMatrix(SqrMatrix&&) = default;
        SqrMatrix& operator=(const SqrMatrix&) = default;
        SqrMatrix& operator=(SqrMatrix&) = default;
        std::vector<double>& operator[](int index){return m[index];}
        double at(int row, int col) const {return m[row][col];}
        void plus_equals(int row, int col, double val) {m[row][col] += val;}
        void minus_equals(int row, int col, double val) {m[row][col] -= val;}
        void assign(int row, int col, double val) {m[row][col] = val;}
        void inner_assign(int a_row, int a_col, int b_row, int b_col) {m[a_row][a_col] = m[b_row][b_col];}
        int size() const {return n;}
        bool is_empty() { return n == 0; }

        void multiply_t(double mult) {
            for (int i = 0; i < n; i++){
                for (int j = 0; j < n; j++){
                    t[i][j] *= mult;
                }
            }
        }

        void calculate_beta_delta(const std::vector<double>& mult, std::vector<double>& beta_delta) const {
            if (mult.size() != n) throw MathError("Matrix Vector dim mismatch");
            for (int i = 0; i < n; i++){
                beta_delta[i] = 0;
                for (int j = 0; j < n; j++){
                    beta_delta[i] += t[i][j] * mult[j];
                }
            }
        }

        void calculate_t_matrix_times_vec(const double *mult, double *ans) {
            //if (mult.size() != n) throw MathError("Matrix Vector dim mismatch"); 
            for (int i = 0; i < n; i++){
                ans[i] = 0;
                for (int j = 0; j < n; j++) {
                    ans[i] += t[i][j] * mult[j];
                }
            }
        }

        void calculate_t_matrix_times_vec(const std::vector<double>& mult, double *ans) {
            //if (mult.size() != n) throw MathError("Matrix Vector dim mismatch"); 
            for (int i = 0; i < n; i++){
                ans[i] = 0;
                for (int j = 0; j < n; j++) {
                    ans[i] += t[i][j] * mult[j];
                }
            }
        }

        void calculate_t_matrix_times_vec(const double *mult, std::vector<double>& ans) {
            //if (mult.size() != n) throw MathError("Matrix Vector dim mismatch"); 
            for (int i = 0; i < n; i++){
                ans[i] = 0;
                for (int j = 0; j < n; j++) {
                    ans[i] += t[i][j] * mult[j];
                }
            }
        }

        #ifdef DEBUG
        friend ostream& operator<<(ostream& os, const SqrMatrix& matrix){
            for (auto &row:matrix.m){
                for (auto xx: row){
                    os << xx << "\t";
                }
                os << endl;
            }
            return os;
        }
        #endif
        void T() {
            for (int i = 0; i < n; i++) {
                for (int j = 0; j < n; j++) {
                    t[i][j] = cof[j][i];
                }
            }
        }
        
        void COF() {
            if (n == 1) {
                cof[0][0] = 1;
                return;
            }
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    cof[i][j] = 0;
                    if (i < n - 1 && j < n - 1 ) {
                        (*sub)[i][j] = 0;
                    }
                } 
            }
            for (int x = 0; x < n; x++){
                for (int y = 0; y < n; y++){
                    int subi = 0;
                    for (int i = 0; i < n; i++){
                        if (i == x) continue;
                        int subj = 0;
                        for (int j = 0; j < n; j++){
                            if (j == y) continue;
                            (*sub)[subi][subj] = m[i][j];
                            subj++;
                        }
                        subi++;
                    }
                    cof[x][y] = (-2 * ((x + y) & 1) + 1) * sub->DET();
                }
            }
        }

        void oblivious_COF() {
            if (n == 1) {
                cof[0][0] = 1;
                return;
            }
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    cof[i][j] = 0;
                    if (i < n - 1 && j < n - 1 ) {
                        (*sub)[i][j] = 0;
                    }
                } 
            }
            for (int x = 0; x < n; x++){
                for (int y = 0; y < n; y++){
                    int subi = 0;
                    for (int i = 0; i < n; i++){
                        if (i == x) continue;
                        int subj = 0;
                        for (int j = 0; j < n; j++){
                            if (j == y) continue;
                            (*sub)[subi][subj] = m[i][j];
                            subj++;
                        }
                        subi++;
                    }
                    cof[x][y] = (2 * !((x + y) & 1) - 1) * sub->oblivious_DET();
                }
            }
        }

        double DET() {
            if (n <= 0) {
                return 0;
            }
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    det[i][j] = m[i][j];
                }
            }

            double sign = 1;

            for (int k = 0; k < n - 1; k++) {
                //Pivot - row swap needed
                if (det[k][k] == 0) {
                    int l = 0;
                    for (l = k + 1; l < n; l++) {
                        if (det[l][k] != 0) {
                            sign = -sign;
                            std::swap(det[l], det[k]);
                            break;
                        }
                    }

                    // No entries != 0 found in column k -> det = 0
                    if (l == n) {
                        return 0;
                    }
                } 

                const double kk = det[k][k];
                double kk_minus_one;
                if (k > 0) {
                    kk_minus_one = det[k - 1][k - 1];
                } 
                //Apply formula
                for (int i = k + 1; i < n; i++) {
                    double *det_i = det[i];
                    for (int j = k + 1; j < n; j++) {
                        det_i[j] = kk * det_i[j] - det_i[k] * det[k][j];
                        if (k > 0) {
                            det_i[j] /= kk_minus_one;
                        }
                    }
                }
            }

            return sign * det[n - 1][n - 1];
        }

        double oblivious_DET() {
            if (n <= 0) {
                return 0;
            }
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    det[i][j] = m[i][j];
                }
            }

            double sign = 1;
            int swap_always_found = 1;

            for (int k = 0; k < n - 1; k++) {

                // We need to always assume a swap might be needed since we can't branch!
                int kk_is_zero = det[k][k] == 0;
                int swap_not_found = 1;
                for (int l = k + 1; l < n; l++) {
                    int lk_is_not_zero = det[l][k] != 0;
                    int do_swap = kk_is_zero & lk_is_not_zero;

                    // Once we do one swap we're supposed to break - we can simulate this by
                    // only allowing one swap to happen once per loop.
                    swap_not_found &= !do_swap;
                    do_swap &= swap_not_found;

                    // If we are doing the swap, negate the sign and swap l and k
                    // If we aren't, keep the sign the same and do "identity" swap
                    sign = predicated_assignment(do_swap, sign, -sign);
                    for (int swap_idx = 0; swap_idx < n; swap_idx++) {
                        double k_val = det[k][swap_idx];
                        double l_val = det[l][swap_idx];
                        tmpL[swap_idx] = predicated_assignment(do_swap, l_val, k_val);
                        tmpK[swap_idx] = predicated_assignment(do_swap, k_val, l_val);
                    }
                    std::swap(det[l], tmpL);
                    std::swap(det[k], tmpK);
                }

                swap_always_found &= !kk_is_zero | !swap_not_found;

                const double kk = det[k][k];
                double kk_minus_one;
                if (k > 0) {
                    kk_minus_one = det[k - 1][k - 1];
                } 
                //Apply formula
                for (int i = k + 1; i < n; i++) {
                    double *det_i = det[i];
                    for (int j = k + 1; j < n; j++) {
                        det_i[j] = kk * det_i[j] - det_i[k] * det[k][j];
                        if (k > 0) {
                            det_i[j] /= kk_minus_one;
                        }
                    }
                }
            }

            // Return the expected result, unless there was a case where no 0 entries were found
            return predicated_assignment(swap_always_found, 0, sign * det[n - 1][n - 1]);
        }

        void INV() {
            double det_res = DET();
            if (det_res == 0) {
                throw MathError("Cannot find inverse(det = 0)");
            }
            COF();
            T();
            multiply_t(1 / det_res);
        }

        void oblivious_INV() {
            double det_res = oblivious_DET();
            oblivious_COF();
            T();
            // It's ok to do 1 / 0 with doubles, the error will propogate to the final result
            // without ruining the oblivious nature
            multiply_t(1 / det_res);
        }
        
        void print() {
            for (int i = 0; i < n; i++){
                for (int j = 0; j < n; j++) {
                    std::cout << m[i][j] << " ";
                }
                std::cout << std::endl;
            }
        }
};


#endif
