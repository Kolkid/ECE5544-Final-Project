//This program gives a test case where there is a 
// load that is not hoistable because of a loop
// dependency
#include <stdlib.h>

volatile int sink;
void ex3(int **pp, int *a, int *b, int n) {
    int *p = a;
    for (int i = 0; i < n; i++) {

        // Load candidate: *p
        int v = *p;//not hoistable
        if (i == n/2)
            p = b;
        *pp = &i;
        sink += v;
    }
}
