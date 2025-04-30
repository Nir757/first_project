#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>

#define MAX_SIZE 1025
#define MAX_ARG 7 // command + 6 arguments
#define MAX_DANG 1000

int space_error(char str[]);
int input_check(char* input, char* original_input, char* command[], int* arg_count);
int split_to_args(char* command[], char* input);
int dang_commands_check(char* dng_cmds[], char* original_input, int dng_count, char* command[], char* print_err);
void execute_command(char* command[], char* original_input, FILE* exec_times, int* cmd,
    double* total_time, double* last_cmd_time, double* avg_time, double* min_time, double* max_time);
void free_memory(char* command[], int arg_count);
void free_dng_cmds(char* dng_cmds[], int dng_count);

int main(int argc, char* argv[])
{
    char line[MAX_SIZE];

    if (argc < 3) {
        fprintf(stderr, "Error: please include two files\n");
        exit(1);
    }

    FILE* dangerous_commands = fopen(argv[1], "r");
    if (dangerous_commands == NULL) {
        fprintf(stderr, "ERR\n");
        exit(1);
    }

    FILE* exec_times = fopen(argv[2], "a");
    if (exec_times == NULL) {
        fprintf(stderr, "ERR\n");
        fclose(dangerous_commands);
        exit(1);
    }

    char* dng_cmds[MAX_DANG];
    int dng_count = 0;
    while (fgets(line, MAX_SIZE, dangerous_commands)) {
        line[strcspn(line, "\n")] = '\0';

        int len = strlen(line);
        while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\r')) {
            line[len - 1] = '\0';
            len--;
        }

        if (len == 0)
            continue;

        dng_cmds[dng_count] = malloc(len + 1);
        if (dng_cmds[dng_count] == NULL) {
            perror("malloc");
            fclose(dangerous_commands);
            fclose(exec_times);
            exit(1);
        }

        strcpy(dng_cmds[dng_count], line);
        dng_count++;
        if (dng_count >= MAX_DANG)
            break;
    }

    fclose(dangerous_commands);

    int cmd = 0;
    int dangerous_cmd_blocked = 0;
    int dangerous_cmd_warning = 0;

    double last_cmd_time = 0;
    double total_time = 0;
    double avg_time = 0;
    double min_time = 0;
    double max_time = 0;

    while (1) {
        printf("#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:%.5f|avg_time:%.5f|min_time:%.5f|max_time:%.5f>>",
               cmd, dangerous_cmd_blocked, last_cmd_time, avg_time, min_time, max_time);

        char input[MAX_SIZE];
        char* command[MAX_ARG + 1];

        if (fgets(input, MAX_SIZE, stdin) == NULL) {
            perror("fgets");
            fclose(exec_times);
            free_dng_cmds(dng_cmds, dng_count);
            exit(1);
        }

        char original_input[MAX_SIZE];
        int arg_count = 0;
        int error = input_check(input, original_input, command, &arg_count);

        if (error != 0)
            continue;

        if (strcmp(command[0], "done") == 0) {
            printf("%d\n", dangerous_cmd_blocked);
            free_memory(command, arg_count);
            free_dng_cmds(dng_cmds, dng_count);
            fclose(exec_times);
            exit(0);
        }

        char print_err[MAX_SIZE];
        int dangerous_status = dang_commands_check(dng_cmds, original_input, dng_count, command, print_err);

        if (dangerous_status == 2) {
            printf("ERR: Dangerous command detected (\"%s\"). Execution prevented.\n", print_err);
            dangerous_cmd_blocked++;
            free_memory(command, arg_count);
            continue;
        }

        if (dangerous_status == 1) {
            printf("WARNING: Command similar to dangerous command (\"%s\"). Proceed with caution.\n", print_err);
            dangerous_cmd_warning++;
        }

        execute_command(command, original_input, exec_times, &cmd, &total_time,
                       &last_cmd_time, &avg_time, &min_time, &max_time);

        free_memory(command, arg_count);
    }


    fclose(exec_times);
    free_dng_cmds(dng_cmds, dng_count);
    return 0;
}

int input_check(char* input, char* original_input, char* command[], int* arg_count)
{
    int error = 0;

    input[strcspn(input, "\n")] = 0;
    strncpy(original_input, input, MAX_SIZE - 1);
    original_input[MAX_SIZE - 1] = '\0';

    *arg_count = 0;

    // Check for double spaces
    if (space_error(original_input) == 1) {
        printf("ERR_SPACE\n");
        error = -1;
    }

    // Split input into arguments
    *arg_count = split_to_args(command, input);
    
    // Check if too many arguments
    if (*arg_count > MAX_ARG) 
    {
        printf("ERR_ARGS\n");
        error = -1;
    }

    if (error == -1) {
        free_memory(command, *arg_count);
        return error;
    }

    return error;
}

int space_error(char str[])
{
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == ' ' && str[i + 1] == ' ')
            return 1;
    }
    return 0;
}

int split_to_args(char* command[], char* input)
{
    char* token = strtok(input, " ");
    int arg_count = 0;

    while (token != NULL) {
        command[arg_count] = malloc(strlen(token) + 1);
        if (command[arg_count] == NULL) {
            perror("malloc");
            exit(1);
        }

        strcpy(command[arg_count], token);
        arg_count++;
        token = strtok(NULL, " ");
    }

    command[arg_count] = NULL;
    return arg_count;
}

int dang_commands_check(char* dng_cmds[], char* original_input, int dng_count, char* command[], char* print_err)
{
    int dang_status = 0;

    for (int i = 0; i < dng_count; i++) {
        if (strcmp(dng_cmds[i], original_input) == 0) {
            dang_status = 2;
            strcpy(print_err, dng_cmds[i]);
            break;
        }

        char original_dang_copy[MAX_SIZE];
        strcpy(original_dang_copy, dng_cmds[i]);

        char* dang_cmd = strtok(original_dang_copy, " ");
        if (dang_cmd != NULL && strcmp(dang_cmd, command[0]) == 0) {
            dang_status = 1;
            strcpy(print_err, dng_cmds[i]);
        }
    }

    return dang_status;
}

void execute_command(char* command[], char* original_input, FILE* exec_times, int* cmd,
                    double* total_time, double* last_cmd_time, double* avg_time, double* min_time, double* max_time)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        fclose(exec_times);
        exit(1);
    }
    else if (pid == 0) {
        execvp(command[0], command);
        perror("execvp");
        exit(1);
    }
    else {
        struct timeval start, end;
        gettimeofday(&start, NULL);

        int status;
        wait(&status);

        gettimeofday(&end, NULL);
        double runtime = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            (*cmd)++;
            *total_time += runtime;
            *last_cmd_time = runtime;
            *avg_time = *total_time / *cmd;

            if (runtime > *max_time)
                *max_time = runtime;

            if (runtime < *min_time || *min_time == 0)
                *min_time = runtime;

            fprintf(exec_times, "%s : %.5f sec\n", original_input, runtime);
            fflush(exec_times);
        }
    }
}

void free_memory(char* command[], int arg_count)
{
    for (int i = 0; i < arg_count; i++) {
        free(command[i]);
    }
}

void free_dng_cmds(char* dng_cmds[], int dng_count)
{
    for (int i = 0; i < dng_count; i++) {
        if (dng_cmds[i] != NULL) {
            free(dng_cmds[i]);
        }
    }
}