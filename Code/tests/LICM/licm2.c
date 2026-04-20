#include <stdio.h>
int test_licm(int *A, int n) {
    int sum = 0;
    int c1 = 7;
    int c2 = n * 3;
    for (int i = 0; i < n; i++) {
        int x = A[i];
        int y = c1 + c2;
        int z = y * 2;

        sum += x + z;
    }
    return sum;
}
int main() {
    int A[5] = {1, 2, 3, 4, 5};
    printf("%d\n", test_licm(A, 5));
    return 0;
}
