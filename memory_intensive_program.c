#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALLOC_SIZE (1024 * 1024) // 1MB chunks

int main() {
    long total_allocated = 0;
    void *ptr;
    
    printf("Starting memory allocation test...\n");
    
    while(1) {
        ptr = malloc(ALLOC_SIZE);
        if (ptr == NULL) {
            printf("Allocation failed after %ld MB\n", total_allocated / (1024 * 1024));
            return 1;
        }
        
        // Fill the memory to ensure it's actually allocated
        memset(ptr, 1, ALLOC_SIZE);
        
        total_allocated += ALLOC_SIZE;
        printf("\rAllocated: %ld MB", total_allocated / (1024 * 1024));
        fflush(stdout);
        
        // Don't free - we want to consume all available memory
    }
    
    return 0;
} 