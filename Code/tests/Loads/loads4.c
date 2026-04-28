//This program gives a test case where there are multiple
// loads to be hoisted, some of which can and cannot be
// done by Steensgaard
#include <stddef.h>
#include <stdint.h>

volatile int sink;
typedef struct {
    int scale;
    int bias;
    int offset;
    int factor;
} Params;

void big_invariant_test(Params *params, const int * restrict table, const int * restrict coeffs, int * restrict out, int n, int m)
{
    //These loads are loop-invariant:
    int scale  = params->scale;
    int bias   = params->bias;
    int offset = params->offset;
    int factor = params->factor;
    int t0 = table[0];
    int t1 = table[1];
    int c0 = coeffs[0];
    int c1 = coeffs[1];
    for (int i = 0; i < n; i++) {
        //These loads are invariant w.r.t the *inner* loop:
        int row_bias   = params->bias;
        int row_factor = params->factor;
        int base_coeff = coeffs[i % 2];
        for (int j = 0; j < m; j++) {
            //All loads below are invariant w.r.t j:
            int inner_scale  = params->scale;
            int inner_offset = params->offset;
            int t = table[(i + j) & 1];
            //No stores alias params, table, or coeffs → all safe to hoist
            int val = inner_scale * t
                    + row_bias
                    + base_coeff
                    + inner_offset
                    + t0 + t1 + c0 + c1
                    + factor;
            out[i * m + j] = val;
        }
    }
    sink = out[0];
}