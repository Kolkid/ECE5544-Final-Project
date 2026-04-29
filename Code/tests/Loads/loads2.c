//This program gives a test case where there is a 
// load that Andersen can hoist but Steensgaard cannot
#include <stdlib.h>

volatile int sink;
void ex2(int **pp, int **qq, int *x, int *y, int n) {
    *pp = x;
    *qq = y;
    int *p = *pp;
    int *q = *qq;
    for (int i = 0; i < n; i++) {
        int v = *p;     // Hoistable by Andersen but not by Steensgaard
        *q = i;         
        sink += v;
    }
}
int main() {
    int x = 0, y = 0;
    int *px = &x;
    int *py = &y;
    int *pp = NULL;
    int *qq = NULL;
    ex2(&pp, &qq, px, py, 10);
    return 0;
}