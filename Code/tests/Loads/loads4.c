//This program gives a test case where there are multiple
// loads to be hoisted, some of which can and cannot be
// done by Steensgaard
#include <stdlib.h>

volatile int sink;
struct Params {
    int a, b, c, d;
};
void big_test(struct Params *params, int *table, int *coeffs, int *out)
{
    //lots of loop invariant loads
    int A = params->a;
    int B = params->b;
    int C = params->c;
    int D = params->d;

    int T0 = table[0];
    int T1 = table[1];
    int T2 = table[2];
    int T3 = table[3];

    int K0 = coeffs[0];
    int K1 = coeffs[1];
    int K2 = coeffs[2];
    int K3 = coeffs[3];
    for (int i = 0; i < 50; i++) {
        int v =
            A + B + C + D +
            T0 + T1 + T2 + T3 +
            K0 + K1 + K2 + K3;
        out[i] = v;
        sink += v;
    }
}
int main() {
    struct Params params = { 1, 2, 3, 4 };
    int table[4]  = { 10, 20, 30, 40 };
    int coeffs[4] = { 7,  9, 11, 13 };
    int out[100];
    big_test(&params, table, coeffs, out);
    return 0;
}