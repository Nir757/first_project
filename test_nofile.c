#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int main() {
    int fds[100];  // Array to store file descriptors
    int count = 0;
    
    printf("Trying to open files...\n");
    
    // Try to open 100 files
    for (int i = 0; i < 100; i++) {
        char filename[32];
        sprintf(filename, "testfile%d.txt", i);
        
        fds[count] = open(filename, O_WRONLY | O_CREAT, 0644);
        if (fds[count] == -1) {
            printf("Failed to open file %d: %s\n", i, strerror(errno));
            break;
        }
        printf("Successfully opened file %d\n", i);
        count++;
    }
    
    printf("\nTotal files opened: %d\n", count);
    
    // Close all opened files
    for (int i = 0; i < count; i++) {
        close(fds[i]);
    }
    
    return 0;
} 