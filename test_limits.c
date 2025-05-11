#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <string.h>
#include <signal.h>

void handle_sigcpu(int signo) { 
    printf("CPU time limit exceeded!\n"); 
    exit(1); 
}

void handle_sigfsz(int signo) { 
    printf("File size limit exceeded!\n"); 
    exit(1); 
}

void handle_sigmem(int signo) { 
    printf("Memory limit exceeded!\n"); 
    exit(1); 
}

void handle_signof(int signo) { 
    printf("Open files limit exceeded!\n"); 
    exit(1); 
}

void test_cpu_limit() {
    printf("Testing CPU limit...\n");
    while(1) {
        // Infinite loop to consume CPU
    }
}

void test_memory_limit() {
    printf("Testing memory limit...\n");
    while(1) {
        void* ptr = malloc(1024 * 1024); // Allocate 1MB at a time
        if (!ptr) {
            printf("Memory limit exceeded!\n");
            exit(1);
        }
    }
}

void test_file_size_limit() {
    printf("Testing file size limit...\n");
    FILE* f = fopen("testfile", "w");
    if (!f) {
        perror("fopen");
        return;
    }
    
    while(1) {
        fwrite("A", 1, 1, f);
    }
}

void test_open_files_limit() {
    printf("Testing open files limit...\n");
    FILE* files[1000];
    int i = 0;
    
    while(1) {
        char filename[32];
        sprintf(filename, "testfile%d", i);
        files[i] = fopen(filename, "w");
        if (!files[i]) {
            printf("Open files limit exceeded!\n");
            exit(1);
        }
        i++;
    }
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGXCPU, handle_sigcpu);
    signal(SIGXFSZ, handle_sigfsz);
    signal(SIGSEGV, handle_sigmem);
    signal(SIGUSR1, handle_signof);

    if (argc != 2) {
        printf("Usage: %s [cpu|mem|fsize|nofile]\n", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "cpu") == 0) {
        test_cpu_limit();
    } else if (strcmp(argv[1], "mem") == 0) {
        test_memory_limit();
    } else if (strcmp(argv[1], "fsize") == 0) {
        test_file_size_limit();
    } else if (strcmp(argv[1], "nofile") == 0) {
        test_open_files_limit();
    } else {
        printf("Invalid test type\n");
        return 1;
    }
    
    return 0;
} 