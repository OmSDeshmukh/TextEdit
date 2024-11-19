#include <stdio.h>

int main() {
    int *p = NULL;
    *p = 42; // This will cause a segmentation fault
    return 0;
}