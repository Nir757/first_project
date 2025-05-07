#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_ARGS 6
#define MAX_CMD_LEN 256

// Function to check if command is dangerous
int is_dangerous_command(const char *cmd) {
    const char *dangerous[] = {"rm", "format", "mkfs", "dd", NULL};
    for (int i = 0; dangerous[i] != NULL; i++) {
        if (strcmp(cmd, dangerous[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Function to check if command is similar to dangerous command
int is_similar_to_dangerous(const char *cmd) {
    const char *dangerous[] = {"rm", "format", "mkfs", "dd", NULL};
    for (int i = 0; dangerous[i] != NULL; i++) {
        if (strstr(cmd, dangerous[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

// Function to check for space errors (multiple spaces)
int has_space_error(const char *cmd) {
    int space_count = 0;
    for (int i = 0; cmd[i] != '\0'; i++) {
        if (cmd[i] == ' ') {
            space_count++;
            if (space_count > 1) return 1;
        } else {
            space_count = 0;
        }
    }
    return 0;
}

// Function to validate a single command
int validate_command(const char *cmd, char *error_msg) {
    if (strlen(cmd) == 0) {
        strcpy(error_msg, "ERROR: Empty command");
        return 0;
    }

    if (has_space_error(cmd)) {
        strcpy(error_msg, "ERROR: Multiple spaces detected between arguments");
        return 0;
    }

    char cmd_copy[MAX_CMD_LEN];
    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    char *token = strtok(cmd_copy, " ");
    int count = 0;
    char *args[MAX_ARGS + 2];  // +2 to detect overflow

    // Store command
    if (token == NULL) {
        strcpy(error_msg, "ERROR: Empty command");
        return 0;
    }
    args[count++] = token;

    // Check if command is dangerous
    if (is_dangerous_command(token)) {
        sprintf(error_msg, "ERROR: Command '%s' is blocked for safety reasons", token);
        return 0;
    }

    // Parse arguments
    while (token != NULL) {
        token = strtok(NULL, " ");
        if (token != NULL) {
            if (count > MAX_ARGS) {
                strcpy(error_msg, "ERROR: Too many arguments (max 6 allowed)");
                return 0;
            }
            args[count] = token;
            count++;
        }
    }

    // Check if command is similar to dangerous
    if (is_similar_to_dangerous(args[0])) {
        sprintf(error_msg, "WARNING: Command '%s' is similar to a dangerous command", args[0]);
        // Don't return 0, just a warning
    }

    return 1;
}

// Function to split string by delimiter and return array of strings
char **split_string(const char *str, const char *delim, int *count) {
    char *str_copy = strdup(str);
    char **result = NULL;
    *count = 0;
    
    char *token = strtok(str_copy, delim);
    while (token != NULL) {
        result = realloc(result, (*count + 1) * sizeof(char*));
        // Trim leading spaces
        while (*token == ' ') token++;
        // Trim trailing spaces
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ') end--;
        *(end + 1) = '\0';
        
        result[*count] = strdup(token);
        (*count)++;
        token = strtok(NULL, delim);
    }
    
    free(str_copy);
    return result;
}

void free_string_array(char **arr, int count) {
    for (int i = 0; i < count; i++) {
        free(arr[i]);
    }
    free(arr);
}

void run_test(const char *test_name, const char *cmd) {
    printf("\n=== Test: %s ===\n", test_name);
    printf("Command: %s\n", cmd);
    printf("----------------------------------------\n");

    // Split the command by pipe
    int cmd_count = 0;
    char **commands = split_string(cmd, "|", &cmd_count);

    if (cmd_count == 0) {
        printf("ERROR: No valid commands found\n");
        free_string_array(commands, cmd_count);
        return;
    }

    // Validate each command in the pipeline
    char error_msg[MAX_CMD_LEN];
    for (int i = 0; i < cmd_count; i++) {
        printf("Validating command %d: '%s'\n", i + 1, commands[i]);
        
        if (strlen(commands[i]) == 0) {
            printf("ERROR: Empty command in pipeline\n");
            free_string_array(commands, cmd_count);
            return;
        }

        if (!validate_command(commands[i], error_msg)) {
            printf("%s\n", error_msg);
            free_string_array(commands, cmd_count);
            return;
        } else if (strstr(error_msg, "WARNING") == error_msg) {
            printf("%s\n", error_msg);
        }
    }

    printf("All commands validated successfully\n");
    free_string_array(commands, cmd_count);
}

int main() {
    // Basic command tests
    run_test("6 Arguments Test", "echo arg1 arg2 arg3 arg4 arg5 arg6");
    run_test("7 Arguments Test", "echo arg1 arg2 arg3 arg4 arg5 arg6 arg7");
    run_test("Empty Command Test", "");
    run_test("Space Error Test", "echo  arg1   arg2");

    // Combined error tests
    run_test("Space and Args Error", "echo  arg1   arg2 arg3 arg4 arg5 arg6 arg7");
    run_test("Space and Dangerous", "rm  -rf  /");

    // Pipe tests with various combinations
    run_test("Simple Pipe Test", "echo hello | grep h");
    run_test("Multi Pipe Test", "echo hello | grep h | wc -l");
    run_test("Pipe with Space Error", "echo  hello | grep h");
    run_test("Pipe with Space Error (Second Command)", "echo hello |  grep h");
    run_test("Pipe with Args Error", "echo arg1 arg2 arg3 arg4 arg5 arg6 arg7 | grep h");
    run_test("Pipe with Args Error (Second Command)", "echo hello | grep arg1 arg2 arg3 arg4 arg5 arg6 arg7");
    run_test("Pipe with Dangerous Command", "echo hello | rm -rf /");
    run_test("Pipe with Similar to Dangerous", "echo hello | rmdir /tmp");

    // Tee specific tests
    run_test("Tee Basic Test", "echo hello | tee file.txt");
    run_test("Tee with Multiple Files", "echo hello | tee file1.txt file2.txt file3.txt");
    run_test("Tee with Too Many Files", "echo hello | tee f1.txt f2.txt f3.txt f4.txt f5.txt f6.txt f7.txt");
    run_test("Tee with Space Error", "echo hello | tee  file.txt");
    run_test("Complex Tee Pipeline", "echo hello | grep h | tee file.txt | wc -l");
    run_test("Tee with Dangerous Filename", "echo hello | tee /root/system.txt");

    // Edge cases
    run_test("Empty Pipe Test", "echo hello | | grep h");
    run_test("Multiple Pipes with Spaces", "echo hello |  grep h  |   wc -l");
    run_test("Complex Error Combination", "rm  -rf / | tee  file.txt | grep  pattern arg1 arg2 arg3 arg4 arg5 arg6");

    return 0;
} 