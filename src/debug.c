#include <stdio.h>
#include <string.h>

int main() {
    char test1[] = "a b c d e f g h i j k l m n o p";
    char test2[] = "echo hi its me";
    char test3[] = "echo hi";
    
    // First tokenize test1 (simulating too many arguments)
    printf("Tokenizing first string:\n");
    char *token = strtok(test1, " ");
    int count = 0;
    
    while (token != NULL && count < 8) {
        printf("Token %d: %s\n", count, token);
        token = strtok(NULL, " ");
        count++;
    }
    
    // Now try to tokenize test2 (simulating dangerous command)
    printf("\nTokenizing second string:\n");
    char *cmd = strtok(test2, " ");
    printf("First token: %s\n", cmd);
    
    // Reset strtok state and try again with test3
    printf("\nResetting strtok with empty string:\n");
    strtok("", " ");
    
    printf("\nTokenizing third string:\n");
    char *cmd2 = strtok(test3, " ");
    printf("First token: %s\n", cmd2);
    
    return 0;
} 