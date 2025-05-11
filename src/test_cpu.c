#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main() {
    printf("Starting CPU-intensive operation...\n");
    
    // Just consume CPU time with a long loop
    for (long i = 0; i < 10000000000L; i++) {
        if (i % 100000000 == 0) {
            printf("Progress: %ld\n", i);
        }
    }
    
    printf("Completed!\n");
    return 0;
} 