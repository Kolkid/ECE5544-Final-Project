#include <stdio.h>
int test_nested_invariants(int *A, int n, int x, int y) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        int inv1 = x * y;
        int inv2 = inv1 + 5;
        sum += A[i] * inv2;
    }
    return sum;
}
int main() {
    int A[5] = {1, 2, 3, 4, 5};
    int result = test_nested_invariants(A, 5, 7, 3);
    printf("Result = %d\n", result);
    return 0;
}