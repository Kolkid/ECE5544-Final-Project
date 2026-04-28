//This program gives a test case where there is a 
// load that is not hoistable by either analyis
#include <stdlib.h>

volatile int sink;
void ex1(int *a, int *b, int cond, int n) {
    int *p, *q;

    if (cond) {
        p = a;
        q = a;
    } else {
        p = b;
        q = b;
    }
    for (int i = 0; i < n; i++) {
        int x = *p;   //not hoistable
        *q = i;
        sink += x;
    }
}