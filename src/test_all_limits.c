#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

// Signal handlers
void handle_sigcpu(int signo) {
    printf("CPU time limit exceeded!\n");
    exit(1);
}

void handle_sigfsz(int signo) {
    printf("File size limit exceeded!\n");
    exit(1);
}

void handle_sigmem(int signo) {
    printf("Memory allocation failed!\n");
    exit(1);
}

void handle_signof(int signo) {
    printf("Too many open files!\n");
    exit(1);
}

// Test functions
void test_cpu_limit() {
    printf("Testing CPU limit...\n");
    
    // Set CPU time limit to 2 seconds
    struct rlimit rl;
    rl.rlim_cur = 2;  // 2 seconds soft limit
    rl.rlim_max = 3;  // 3 seconds hard limit
    
    if (setrlimit(RLIMIT_CPU, &rl) == -1) {
        perror("setrlimit CPU");
        return;
    }
    
    // Consume CPU time
    for (long i = 0; i < 10000000000L; i++) {
        if (i % 100000000 == 0) {
            printf("CPU Progress: %ld\n", i);
        }
    }
    
    printf("CPU test completed without triggering limit\n");
}

void test_memory_limit() {
    printf("Testing memory limit...\n");
    
    // Set memory limit to 10MB
    struct rlimit rl;
    rl.rlim_cur = 10 * 1024 * 1024;  // 10MB soft limit
    rl.rlim_max = 12 * 1024 * 1024;  // 12MB hard limit
    
    if (setrlimit(RLIMIT_AS, &rl) == -1) {
        perror("setrlimit MEM");
        return;
    }
    
    // Try to allocate memory beyond the limit
    long allocated = 0;
    for (int i = 0; i < 100; i++) {
        int* block = malloc(1024 * 1024);  // Try to allocate 1MB at a time
        if (block) {
            allocated += 1024 * 1024;
            printf("Allocated: %ld MB\n", allocated / (1024 * 1024));
            // Fill the memory to ensure it's actually allocated
            memset(block, 1, 1024 * 1024);
        } else {
            printf("Memory allocation failed after %ld MB\n", allocated / (1024 * 1024));
            break;
        }
    }
    
    printf("Memory test completed without triggering limit\n");
}

void test_file_size_limit() {
    printf("Testing file size limit...\n");
    
    // Set file size limit to 1MB
    struct rlimit rl;
    rl.rlim_cur = 1 * 1024 * 1024;  // 1MB soft limit
    rl.rlim_max = 2 * 1024 * 1024;  // 2MB hard limit
    
    if (setrlimit(RLIMIT_FSIZE, &rl) == -1) {
        perror("setrlimit FSIZE");
        return;
    }
    
    // Try to create a file larger than the limit
    int fd = open("testfile", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        return;
    }
    
    char buffer[1024];
    memset(buffer, 'A', sizeof(buffer));
    
    for (int i = 0; i < 2048; i++) {  // Try to write 2MB
        if (write(fd, buffer, sizeof(buffer)) == -1) {
            perror("write");
            break;
        }
        if (i % 100 == 0) {
            printf("Written: %d KB\n", i);
        }
    }
    
    close(fd);
    printf("File size test completed without triggering limit\n");
}

void test_open_files_limit() {
    printf("Testing open files limit...\n");
    
    // Set open files limit to 5
    struct rlimit rl;
    rl.rlim_cur = 5;  // 5 files soft limit
    rl.rlim_max = 10; // 10 files hard limit
    
    if (setrlimit(RLIMIT_NOFILE, &rl) == -1) {
        perror("setrlimit NOFILE");
        return;
    }
    
    // Try to open more files than the limit
    int fd[20];
    int count = 0;
    
    for (int i = 0; i < 20; i++) {
        char filename[20];
        sprintf(filename, "testfile%d", i);
        
        fd[i] = open(filename, O_WRONLY | O_CREAT, 0644);
        if (fd[i] == -1) {
            printf("Failed to open file #%d: %s\n", i, strerror(errno));
            break;
        }
        count++;
        printf("Opened file #%d\n", i);
    }
    
    printf("Successfully opened %d files\n", count);
    
    // Close all opened files
    for (int i = 0; i < count; i++) {
        close(fd[i]);
    }
    
    printf("Open files test completed\n");
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGXCPU, handle_sigcpu);
    signal(SIGXFSZ, handle_sigfsz);
    signal(SIGSEGV, handle_sigmem);
    signal(SIGUSR1, handle_signof);
    
    if (argc < 2) {
        printf("Usage: %s [cpu|mem|fsize|nofile|all]\n", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "cpu") == 0 || strcmp(argv[1], "all") == 0) {
        test_cpu_limit();
    }
    
    if (strcmp(argv[1], "mem") == 0 || strcmp(argv[1], "all") == 0) {
        test_memory_limit();
    }
    
    if (strcmp(argv[1], "fsize") == 0 || strcmp(argv[1], "all") == 0) {
        test_file_size_limit();
    }
    
    if (strcmp(argv[1], "nofile") == 0 || strcmp(argv[1], "all") == 0) {
        test_open_files_limit();
    }
    
    return 0;
} 