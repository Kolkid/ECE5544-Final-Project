//This program gives a test case where there is a 
// load that Andersen can hoist but Steensgaard cannot
// as well as a load that both can hoist
#include <stdlib.h>

volatile int sink;
void ex_mixed(int **pp, int **qq,
              int *x, int *y, int *z,
              int n) {
    *pp = x;
    *qq = y;

    int *p = *pp;
    int *q = *qq;
    int *r = z;

    for (int i = 0; i < n; i++) {
        //only Andersen hoists
        int v1 = *p;
        //both hoist
        int v2 = *r;

        *q = i;
        sink += v1 + v2;
    }
}

int main() {
    int x = 1, y = 2, z = 3;
    int *px = &x;
    int *py = &y;
    int *pz = &z;
    int *pp = NULL;
    int *qq = NULL;
    ex_mixed(&pp, &qq, px, py, pz, 10);
    return 0;
}
