//This program gives a test case where there is a 
// load that is not hoistable because of a loop
// dependency
#include <stdlib.h>

volatile int sink;
void ex3(int **pp, int *a, int *b) {
    int *p = a;
    for (int i = 0; i < 10; i++) {
        int n = 10;
        // Load candidate: *p
        int v = *p;//not hoistable
        if (i == n/2)
            p = b;
        *pp = &i;
        sink += v;
    }
}
int main() {
    int a = 1, b = 2;
    int *pa = &a;
    int *pb = &b;
    int *pp = NULL;
    ex3(&pp, pa, pb);
    return 0;
}