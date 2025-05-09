#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/resource.h> // for setrlimit and getrlimit
#include <sys/stat.h> // for file permission constants
#include <signal.h> // for signal handling
#include <errno.h> // for errno
#include <ctype.h> // for isdigit
#include <fcntl.h> // for open, O_WRONLY, O_CREAT, O_APPEND, O_TRUNC, S_IRUSR, S_IWUSR, S_IRGRP, S_IROTH
#include <bits/sigaction.h> // for struct sigaction

#define MAX_SIZE 1025
#define MAX_ARG 7 // command + 6 arguments
#define MAX_DANG 1000

// Resource limit related defines
#define BYTES_IN_KB 1024
#define BYTES_IN_MB (1024 * 1024)
#define BYTES_IN_GB (1024 * 1024 * 1024)

int space_error(char str[]);
void split_string(char* input, char* result[], int* count);
void input_arg_check(int argc);
FILE* open_file(char* filename, char* mode);
int load_dangerous_commands(FILE* dangerous_commands, char* dng_cmds[]);
int split_and_validate(char* input, char* original_input, char* command[]);
void free_resources(char* command[], int arg_count, char* dng_cmds[], int dng_count);
int check_dangerous_command(char* original_input, char* command[], char* dng_cmds[], int dng_count, char* print_err, int* dangerous_cmd_warning, int* dangerous_cmd_blocked, int arg_count);
double execute_command(char* command[], char* original_input, FILE* exec_times);
double handle_pipe(char* input, char* original_input, FILE* exec_times, char* dng_cmds[], int dng_count, int pipe_index, int* dangerous_cmd_warning, int* dangerous_cmd_blocked);
void handle_mytee(char * command[], int right_arg_count);

// Resource limit related declarations
typedef struct {
    int resource_type;  // RLIMIT_CPU, RLIMIT_AS, etc.
    rlim_t soft_limit;
    rlim_t hard_limit;
} resource_limit_t;

int parse_size_value(const char* str, rlim_t* value);
int parse_time_value(const char* str, rlim_t* value);
int parse_resource_value(const char* resource_name, const char* value_str, resource_limit_t* limit);
void show_resource_limits(void);
int set_resource_limit(int resource, rlim_t soft_limit, rlim_t hard_limit);
int handle_rlimit_command(char* command[], int arg_count, FILE* exec_times, int* cmd, 
                         double* total_time, double* last_cmd_time, double* avg_time,
                         double* min_time, double* max_time);
int execute_with_limits(char* command[], char* original_input, FILE* exec_times, resource_limit_t* limits, int limit_count);

int main(int argc, char* argv[])
{
    input_arg_check(argc);

    FILE* dangerous_commands = open_file(argv[1], "r");
    FILE* exec_times = open_file(argv[2], "a");

    char* dng_cmds[MAX_DANG];
    int dng_count = load_dangerous_commands(dangerous_commands, dng_cmds);

    fclose(dangerous_commands);

    int cmd = 0;
    int dangerous_cmd_blocked = 0;
    int dangerous_cmd_warning = 0;

    double last_cmd_time = 0;
    double total_time = 0;
    double avg_time = 0;
    double min_time = 0;
    double max_time = 0;

    while (1) // the mini-shell
    {
        printf("#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:%.5f|avg_time:%.5f|min_time:%.5f|max_time:%.5f>>"
                ,cmd,dangerous_cmd_blocked,last_cmd_time,avg_time,min_time,max_time);

        char input[MAX_SIZE]; //input str
        char original_input[MAX_SIZE]; //to have the original after using splitting
        char* command[MAX_ARG + 1]; //array for arguments + NULL

        if (fgets(input, MAX_SIZE, stdin) == NULL) //getting input
        {
            perror("fgets");
            exit(1);
        }

        input[strcspn(input, "\n")] = 0; //remove newline character after using fgets and avoiding execvp error
        strcpy(original_input, input);

        // Check for background process
        int background = 0;
        int len = strlen(input);
        if (len > 0 && input[len-1] == '&')
        {
            background = 1;
            input[len-1] = '\0';  // Remove the &
            // Remove trailing spaces
            len = strlen(input);
            while (len > 0 && input[len-1] == ' ')
            {
                input[len-1] = '\0';
                len--;
            }
        }

        // Check for stderr redirection
        char* stderr_file = NULL;
        for (int i = 0; i < len - 1; i++)
        {
            if (input[i] == '2' && input[i+1] == '>')
            {
                input[i] = '\0';  // Split the command
                stderr_file = input + i + 2;
                // Skip leading spaces in filename
                while (*stderr_file == ' ') stderr_file++;
                break;
            }
        }

        int flag_pipe = 0; //not a fucntion because we only want to check for one pipe
        int pipe_index;
        for (pipe_index = 0; pipe_index < strlen(input); pipe_index++)
        {
            if (input[pipe_index] == '|')
                {
                    if(input[pipe_index - 1] == ' ' && input[pipe_index + 1] == ' ') // Only set flag_pipe if there are spaces on both sides
                    {
                        flag_pipe = 1;
                        break;
                    }
                }
        }

        if (flag_pipe == 1)
        {
            double runtime = handle_pipe(input, original_input, exec_times, dng_cmds, dng_count, pipe_index, &dangerous_cmd_warning, &dangerous_cmd_blocked);

            if (runtime >= 0) // Both commands succeeded
            {
                cmd += 2;  // Both commands were valid and ran
                total_time += runtime;
                last_cmd_time = runtime / 2;  // Average time per command
                avg_time = total_time / cmd;

                double per_cmd_time = runtime / 2;  // Split runtime between the two commands
                if (per_cmd_time > max_time)
                    max_time = per_cmd_time;

                if (per_cmd_time < min_time || min_time == 0)
                {
                    min_time = per_cmd_time;
                }
            }
            else
            {
                printf("ERR_PIPE\n");
            }

            continue; // Skip the regular command processing
        }

        int arg_count = split_and_validate(input, original_input, command);
        if (arg_count == -1) // Error in parsing input
        {
            for (int i = 0; i < MAX_ARG + 1; i++) {
                command[i] = NULL;
            }
            continue;
        }

        if (arg_count == 0) // skip empty input lines
            continue;

        if (strcmp(command[0], "done") == 0) //checking for done - end of terminal
        {
            printf("%d\n", dangerous_cmd_blocked);

            // Only free the current command arguments which we know are valid - free resources caused double free error
            for (int i = 0; i < arg_count; i++) {
                if (command[i] != NULL) {
                    free(command[i]);
                }
            }

            fclose(exec_times);
            exit(0);
        }

        // Handle rlimit command
        if (strcmp(command[0], "rlimit") == 0)
        {
            if (handle_rlimit_command(command, arg_count, exec_times, &cmd, &total_time, &last_cmd_time, &avg_time, &min_time, &max_time))
            {
                for (int i = 0; i < arg_count; i++)
                {
                    if (command[i] != NULL)
                        free(command[i]);
                }
                continue;
            }
        }

        char print_err[MAX_SIZE];
        int danger_status = check_dangerous_command(original_input, command, dng_cmds, dng_count, print_err, &dangerous_cmd_warning, &dangerous_cmd_blocked, arg_count);

        if (danger_status == 1) // Dangerous command detected
        {
            for (int i = 0; i < arg_count; i++)
            {
                free(command[i]);
            }
            continue;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            for (int i = 0; i < arg_count; i++)
                free(command[i]);
            continue;
        }
        else if (pid == 0)
        {
            // Child process
            if (stderr_file != NULL)
            {
                // Redirect stderr to file
                FILE* f = fopen(stderr_file, "w");
                if (f == NULL)
                {
                    perror("fopen");
                    exit(1);
                }
                if (dup2(fileno(f), STDERR_FILENO) == -1)
                {
                    perror("dup2");
                    exit(1);
                }
                fclose(f);
            }

            execvp(command[0], command);
            perror("execvp");
            exit(1);
        }
        else
        {
            // Parent process
            struct timeval start, end;
            gettimeofday(&start, NULL);

            if (!background)
            {
                int status;
                waitpid(pid, &status, 0);

                gettimeofday(&end, NULL);
                double runtime = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

                if (WIFEXITED(status))
                {
                    int exit_code = WEXITSTATUS(status);
                    if (exit_code == 0)
                    {
                        fprintf(exec_times, "%s : %.5f sec\n", original_input, runtime);
                        fflush(exec_times);
                        cmd++;
                        total_time += runtime;
                        last_cmd_time = runtime;
                        avg_time = total_time / cmd;

                        if (runtime > max_time)
                            max_time = runtime;

                        if (runtime < min_time || min_time == 0)
                            min_time = runtime;
                    }
                    else
                    {
                        printf("Command failed with exit code: %d\n", exit_code);
                    }
                }
                else if (WIFSIGNALED(status))
                {
                    printf("Terminated by signal: %d\n", WTERMSIG(status));
                }
            }
        }

        for (int i = 0; i < arg_count; i++)
        {
            free(command[i]);
        }
    }

    return 0;
}

void input_arg_check(int argc)
{
    if(argc < 3)
    {
        fprintf(stderr, "Error: please include two files\n");
        exit(1);
    }
}

FILE* open_file(char* filename, char* mode)
{
    FILE* file = fopen(filename, mode);
    if(file == NULL)
    {
        fprintf(stderr, "ERR\n");
        exit(1);
    }
    return file;
}

int load_dangerous_commands(FILE* dangerous_commands, char* dng_cmds[])
{
    char line[MAX_SIZE];
    int dng_count = 0;

    while(fgets(line, MAX_SIZE, dangerous_commands))
    {
        line[strcspn(line, "\n")] = '\0'; // remove newline character

        int len = strlen(line);
        while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\r')) //this loop is to remove ' ' and \r which caused strcmp to not work (took me hours to understand)
            {
                line[len - 1] = '\0';
                len--;
            }

        if (len == 0)
            continue;

        dng_cmds[dng_count] = malloc(len + 1);
        if (dng_cmds[dng_count] == NULL)
        {
            perror("malloc");
            exit(1);
        }

        strcpy(dng_cmds[dng_count], line); // store in array

        dng_count++;
        if (dng_count >= MAX_DANG)
            break;
    }

    return dng_count;
}

int split_and_validate(char* input, char* original_input, char* command[])
{
    int arg_count = 0;
    int error = 0;  // Track if we have any errors

    int space_err = space_error(input);
    if (space_err == 1) //check for one or more spaces
    {
        printf("ERR_SPACE\n");
        error = 1;
    }

    split_string(input, command, &arg_count);

    // Check if split_string encountered too many arguments
    if (arg_count == -1 || arg_count > MAX_ARG) 
    {
        printf("ERR_ARGS\n");
        error = 1;
    }

    if (error) {
        // Clean up any allocated memory
        for (int i = 0; command[i] != NULL; i++) {
            free(command[i]);
            command[i] = NULL;
        }
        return -1;
    }

    return arg_count;
}

void free_resources(char* command[], int arg_count, char* dng_cmds[], int dng_count)
{
    for (int i = 0; i < arg_count; i++)
    {
        free(command[i]);
    }
    for (int i = 0; i < dng_count; i++)
    {
        free(dng_cmds[i]);
    }
}

int check_dangerous_command(char* original_input, char* command[], char* dng_cmds[], int dng_count, char* print_err, int* dangerous_cmd_warning, int* dangerous_cmd_blocked, int arg_count)
{
    int warning = 0;
    int dang_err = 0;

    for (int i = 0; i < dng_count; i++)
    {
        if (strcmp(dng_cmds[i], original_input) == 0) // exact match
        {
            dang_err = 1;
            strcpy(print_err, dng_cmds[i]);
            break;
        }

        char original_dang_copy[MAX_SIZE];  // warning on command name match
        strcpy(original_dang_copy, dng_cmds[i]);
        char* dang_cmd = strtok(original_dang_copy, " "); // we only need the first word so no need to split the whole string
        if (dang_cmd != NULL && strcmp(dang_cmd, command[0]) == 0)
        {
            warning = 1;
            strcpy(print_err, dng_cmds[i]);
        }
    }

    if (dang_err)
    {
        printf("ERR: Dangerous command detected (\"%s\"). Execution prevented.\n", print_err);
        (*dangerous_cmd_blocked)++;
        return 1;  // Dangerous command
    }
    if (warning)
    {
        printf("WARNING: Command similar to dangerous command (\"%s\"). Proceed with caution.\n", print_err);
        (*dangerous_cmd_warning)++; // Increment the warning counter
        return 2;  // Warning
    }

    return 0;  // No danger
}

double execute_command(char* command[], char* original_input, FILE* exec_times)
{
    pid_t pid;
    pid = fork();
    if (pid < 0)
    {
        perror("fork");
        exit(1);
    }
    else if (pid == 0)
    {
        execvp(command[0], command);
        perror("execvp");
        exit(1);
    }
    else
    {
        struct timeval start, end; //gettimeofday return a special variable type
        gettimeofday(&start, NULL);

        int status;
        wait(&status);

        gettimeofday(&end, NULL);

        double runtime = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        {
            fprintf(exec_times, "%s : %.5f sec\n", original_input, runtime);
            fflush(exec_times);
            return runtime;
        }

        return -1;  // Command failed
    }
}

int space_error(char str[])
{
    for (int i = 0; str[i] != '\0'; i++)
    {
        if (str[i] == ' ' && str[i + 1] == ' ')
            return 1;
    }
    return 0;
}

void split_string(char* input, char* result[], int* count) {
    *count = 0;
    int word_start = 0;
    int input_length = strlen(input);
    
    // Initialize all pointers to NULL
    for (int i = 0; i <= MAX_ARG; i++) {
        result[i] = NULL;
    }
    
    for(int i = 0; i <= input_length && *count < MAX_ARG; i++) {
        if(input[i] == ' ' || input[i] == '\0') {
            if(i > word_start) {
                int word_length = i - word_start;
                result[*count] = malloc(word_length + 1);
                if (result[*count] == NULL) {
                    perror("malloc");
                    // Clean up already allocated memory
                    for (int j = 0; j < *count; j++) {
                        free(result[j]);
                        result[j] = NULL;
                    }
                    exit(1);
                }
                strncpy(result[*count], input + word_start, word_length);
                result[*count][word_length] = '\0';
                (*count)++;
            }
            word_start = i + 1;
        }
    }
    
    // Check if we exceeded MAX_ARG
    if (*count >= MAX_ARG && word_start < input_length) {
        // Clean up if we hit the limit
        for (int i = 0; i < *count; i++) {
            free(result[i]);
            result[i] = NULL;
        }
        *count = -1;  // Signal error
        return;
    }
    
    result[*count] = NULL; // NULL terminate for execvp
}

double handle_pipe(char* input, char* original_input, FILE* exec_times, char* dng_cmds[], int dng_count, int pipe_index, int* dangerous_cmd_warning, int* dangerous_cmd_blocked)
{
    char input_left[MAX_SIZE];
    char input_right[MAX_SIZE];
    struct timeval start, end;
    double runtime = 0;

    strncpy(input_left, input, pipe_index);//splitting to left
    input_left[pipe_index] = '\0';

    int right_start = pipe_index + 2; // +2 to skip the the first space
    strcpy(input_right, &input[right_start]); //splitting to right

    char* left_command[MAX_ARG + 1];
    char* right_command[MAX_ARG + 1];

    int left_arg_count = split_and_validate(input_left, original_input, left_command);
    int right_arg_count = split_and_validate(input_right, original_input, right_command);

    if (left_arg_count == -1 || right_arg_count == -1)
    {
        return -1;
    }

    if (left_arg_count == 0 || right_arg_count == 0)
    {
        return -1;
    }

    // Check if either command is dangerous
    char print_err[MAX_SIZE];
    int danger_status_left = check_dangerous_command(input_left, left_command, dng_cmds, dng_count, print_err, dangerous_cmd_warning, dangerous_cmd_blocked, left_arg_count);

    if (danger_status_left == 1) // Dangerous left command detected
    {
        free_resources(left_command, left_arg_count, NULL, 0);
        free_resources(right_command, right_arg_count, NULL, 0);
        return -1;
    }

    int danger_status_right = check_dangerous_command(input_right, right_command, dng_cmds, dng_count, print_err, dangerous_cmd_warning, dangerous_cmd_blocked, right_arg_count);

    if (danger_status_right == 1) // Dangerous right command detected
    {
        free_resources(left_command, left_arg_count, NULL, 0);
        free_resources(right_command, right_arg_count, NULL, 0);
        return -1;
    }

    int pipe_fd[2];
    if (pipe(pipe_fd) == -1)
    {
        perror("pipe");
        exit(1);
    }

    // Start timing for the entire pipe operation
    gettimeofday(&start, NULL);

    pid_t pid_left = fork();
    if (pid_left < 0)
    {
        perror("fork");
        exit(1);
    }
    else if (pid_left == 0)
    {
        close(pipe_fd[0]); //closing the read end of the pipe
        dup2(pipe_fd[1], STDOUT_FILENO); //redirecting stdout to the write end of the pipe
        close(pipe_fd[1]); //closing the write end of the pipe now that he was redirected

        execvp(left_command[0], left_command);
        perror("execvp");//if we reached here there was an error
        exit(1);
    }
    else
    {
        pid_t pid_right = fork();
        if (pid_right < 0)
        {
            perror("fork");
            exit(1);
        }
        else if (pid_right == 0)
        {
            close(pipe_fd[1]); //closing the write end of the pipe
            dup2(pipe_fd[0], STDIN_FILENO); //redirecting stdin to the read end of the pipe
            close(pipe_fd[0]); //closing the read end of the pipe now that he was redirected

            if (strcmp(right_command[0], "my_tee") == 0)
            {
                if (right_arg_count < 2)
                {
                    fprintf(stderr, "ERR: my_tee requires at least one output file\n");
                    exit(1);
                }
                handle_mytee(right_command, right_arg_count);
                exit(0);
            }

            execvp(right_command[0], right_command);
            perror("execvp");
            exit(1);
        }
        else
        {
            close(pipe_fd[0]);//the parent process also needs to close the read end of the pipe
            close(pipe_fd[1]);

            //waiting for the child processes to finish
            int status1, status2;
            waitpid(pid_left, &status1, 0);
            waitpid(pid_right, &status2, 0);

            gettimeofday(&end, NULL);
            runtime = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

            // Write both commands to exec_times with the same runtime
            if (WIFEXITED(status1) && WEXITSTATUS(status1) == 0)
            {
                fprintf(exec_times, "%s : %.5f sec\n", input_left, runtime);
                fflush(exec_times);
            }

            if (WIFEXITED(status2) && WEXITSTATUS(status2) == 0)
            {
                fprintf(exec_times, "%s : %.5f sec\n", input_right, runtime);
                fflush(exec_times);
            }

            free_resources(left_command, left_arg_count, NULL, 0);
            free_resources(right_command, right_arg_count, NULL, 0);

            // Return the runtime if both commands succeeded
            if (WIFEXITED(status1) && WEXITSTATUS(status1) == 0 &&
                WIFEXITED(status2) && WEXITSTATUS(status2) == 0)
            {
                return runtime;
            }
            return -1;  // Command failed
        }
    }
    return -1;
}

void handle_mytee(char * command[], int right_arg_count)
{
    int append_mode = 0;
    int start_index = 1;

    // Check for the -a option
    if (strcmp(command[1], "-a") == 0) 
    {
        append_mode = 1;
        start_index = 2;
    }

    // Buffer to read from stdin
    char buffer[MAX_SIZE];
    ssize_t bytes_read;

    // Open all files first
    int* file_fds = malloc((right_arg_count - start_index) * sizeof(int));
    if (file_fds == NULL)
    {
        perror("malloc");
        exit(1);
    }

    for (int i = start_index; i < right_arg_count; i++)
    {
        file_fds[i - start_index] = open(command[i], 
            O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC),
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        
        if (file_fds[i - start_index] == -1)
        {
            perror("open");
            // Close already opened files
            for (int j = 0; j < i - start_index; j++)
            {
                close(file_fds[j]);
            }
            free(file_fds);
            exit(1);
        }
    }

    // Read from stdin and write to stdout and files
    while ((bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0)
    {
        // Write to stdout
        if (write(STDOUT_FILENO, buffer, bytes_read) != bytes_read)
        {
            perror("write to stdout");
            break;
        }

        // Write to each file
        for (int i = 0; i < right_arg_count - start_index; i++)
        {
            if (write(file_fds[i], buffer, bytes_read) != bytes_read)
            {
                perror("write to file");
                break;
            }
        }
    }

    if (bytes_read == -1)
    {
        perror("read");
    }

    // Close all files
    for (int i = 0; i < right_arg_count - start_index; i++)
    {
        close(file_fds[i]);
    }
    free(file_fds);
}

int parse_size_value(const char* str, rlim_t* value)
{
    char* endptr;
    double num = strtod(str, &endptr);
    
    if (endptr == str || num < 0) // no conversion or negative value
        return 0;
    
    // Skip whitespace
    while (isspace(*endptr)) endptr++;
    
    // Convert based on unit
    if (*endptr == '\0' || strcmp(endptr, "B") == 0)
        *value = (rlim_t)num;
    else if (strcmp(endptr, "K") == 0 || strcmp(endptr, "KB") == 0)
        *value = (rlim_t)(num * BYTES_IN_KB);
    else if (strcmp(endptr, "M") == 0 || strcmp(endptr, "MB") == 0)
        *value = (rlim_t)(num * BYTES_IN_MB);
    else if (strcmp(endptr, "G") == 0 || strcmp(endptr, "GB") == 0)
        *value = (rlim_t)(num * BYTES_IN_GB);
    else
        return 0;
    
    return 1;
}

int parse_time_value(const char* str, rlim_t* value)
{
    char* endptr;
    double num = strtod(str, &endptr);
    
    if (endptr == str || num < 0) // no conversion or negative value
        return 0;
    
    *value = (rlim_t)num;
    return 1;
}

int parse_resource_value(const char* resource_name, const char* value_str, resource_limit_t* limit)
{
    char* soft_str = strdup(value_str);
    char* hard_str = NULL;
    char* colon = strchr(soft_str, ':');
    
    if (colon != NULL)
    {
        *colon = '\0';
        hard_str = colon + 1;
    }
    
    // Set resource type
    if (strcmp(resource_name, "cpu") == 0)
    {
        limit->resource_type = RLIMIT_CPU;
        if (!parse_time_value(soft_str, &limit->soft_limit))
        {
            free(soft_str);
            return 0;
        }
    }
    else if (strcmp(resource_name, "mem") == 0)
    {
        limit->resource_type = RLIMIT_AS;
        if (!parse_size_value(soft_str, &limit->soft_limit))
        {
            free(soft_str);
            return 0;
        }
    }
    else if (strcmp(resource_name, "fsize") == 0)
    {
        limit->resource_type = RLIMIT_FSIZE;
        if (!parse_size_value(soft_str, &limit->soft_limit))
        {
            free(soft_str);
            return 0;
        }
    }
    else if (strcmp(resource_name, "nofile") == 0)
    {
        limit->resource_type = RLIMIT_NOFILE;
        char* endptr;
        long num = strtol(soft_str, &endptr, 10);
        if (endptr == soft_str || num < 0)
        {
            free(soft_str);
            return 0;
        }
        limit->soft_limit = num;
    }
    else
    {
        free(soft_str);
        return 0;
    }
    
    // If hard limit specified, parse it
    if (hard_str != NULL)
    {
        if (limit->resource_type == RLIMIT_CPU)
        {
            if (!parse_time_value(hard_str, &limit->hard_limit))
            {
                free(soft_str);
                return 0;
            }
        }
        else if (limit->resource_type == RLIMIT_AS || limit->resource_type == RLIMIT_FSIZE)
        {
            if (!parse_size_value(hard_str, &limit->hard_limit))
            {
                free(soft_str);
                return 0;
            }
        }
        else // RLIMIT_NOFILE
        {
            char* endptr;
            long num = strtol(hard_str, &endptr, 10);
            if (endptr == hard_str || num < 0)
            {
                free(soft_str);
                return 0;
            }
            limit->hard_limit = num;
        }
    }
    else
    {
        limit->hard_limit = limit->soft_limit;
    }
    
    free(soft_str);
    return 1;
}

void show_resource_limits(void)
{
    struct rlimit rlim;
    char soft_buf[32], hard_buf[32];
    
    if (getrlimit(RLIMIT_CPU, &rlim) == 0)
    {
        if (rlim.rlim_cur == RLIM_INFINITY)
            strcpy(soft_buf, "unlimited");
        else
            snprintf(soft_buf, sizeof(soft_buf), "%lus", (unsigned long)rlim.rlim_cur);
            
        if (rlim.rlim_max == RLIM_INFINITY)
            strcpy(hard_buf, "unlimited");
        else
            snprintf(hard_buf, sizeof(hard_buf), "%lus", (unsigned long)rlim.rlim_max);
            
        printf("CPU time: soft=%s, hard=%s\n", soft_buf, hard_buf);
    }
    
    if (getrlimit(RLIMIT_AS, &rlim) == 0)
    {
        if (rlim.rlim_cur == RLIM_INFINITY)
            strcpy(soft_buf, "unlimited");
        else if (rlim.rlim_cur >= BYTES_IN_GB)
            snprintf(soft_buf, sizeof(soft_buf), "%luGB", (unsigned long)(rlim.rlim_cur / BYTES_IN_GB));
        else if (rlim.rlim_cur >= BYTES_IN_MB)
            snprintf(soft_buf, sizeof(soft_buf), "%luMB", (unsigned long)(rlim.rlim_cur / BYTES_IN_MB));
        else if (rlim.rlim_cur >= BYTES_IN_KB)
            snprintf(soft_buf, sizeof(soft_buf), "%luKB", (unsigned long)(rlim.rlim_cur / BYTES_IN_KB));
        else
            snprintf(soft_buf, sizeof(soft_buf), "%luB", (unsigned long)rlim.rlim_cur);
            
        if (rlim.rlim_max == RLIM_INFINITY)
            strcpy(hard_buf, "unlimited");
        else if (rlim.rlim_max >= BYTES_IN_GB)
            snprintf(hard_buf, sizeof(hard_buf), "%luGB", (unsigned long)(rlim.rlim_max / BYTES_IN_GB));
        else if (rlim.rlim_max >= BYTES_IN_MB)
            snprintf(hard_buf, sizeof(hard_buf), "%luMB", (unsigned long)(rlim.rlim_max / BYTES_IN_MB));
        else if (rlim.rlim_max >= BYTES_IN_KB)
            snprintf(hard_buf, sizeof(hard_buf), "%luKB", (unsigned long)(rlim.rlim_max / BYTES_IN_KB));
        else
            snprintf(hard_buf, sizeof(hard_buf), "%luB", (unsigned long)rlim.rlim_max);
            
        printf("Memory: soft=%s, hard=%s\n", soft_buf, hard_buf);
    }
    
    if (getrlimit(RLIMIT_FSIZE, &rlim) == 0)
    {
        if (rlim.rlim_cur == RLIM_INFINITY)
            strcpy(soft_buf, "unlimited");
        else if (rlim.rlim_cur >= BYTES_IN_GB)
            snprintf(soft_buf, sizeof(soft_buf), "%luGB", (unsigned long)(rlim.rlim_cur / BYTES_IN_GB));
        else if (rlim.rlim_cur >= BYTES_IN_MB)
            snprintf(soft_buf, sizeof(soft_buf), "%luMB", (unsigned long)(rlim.rlim_cur / BYTES_IN_MB));
        else if (rlim.rlim_cur >= BYTES_IN_KB)
            snprintf(soft_buf, sizeof(soft_buf), "%luKB", (unsigned long)(rlim.rlim_cur / BYTES_IN_KB));
        else
            snprintf(soft_buf, sizeof(soft_buf), "%luB", (unsigned long)rlim.rlim_cur);
            
        if (rlim.rlim_max == RLIM_INFINITY)
            strcpy(hard_buf, "unlimited");
        else if (rlim.rlim_max >= BYTES_IN_GB)
            snprintf(hard_buf, sizeof(hard_buf), "%luGB", (unsigned long)(rlim.rlim_max / BYTES_IN_GB));
        else if (rlim.rlim_max >= BYTES_IN_MB)
            snprintf(hard_buf, sizeof(hard_buf), "%luMB", (unsigned long)(rlim.rlim_max / BYTES_IN_MB));
        else if (rlim.rlim_max >= BYTES_IN_KB)
            snprintf(hard_buf, sizeof(hard_buf), "%luKB", (unsigned long)(rlim.rlim_max / BYTES_IN_KB));
        else
            snprintf(hard_buf, sizeof(hard_buf), "%luB", (unsigned long)rlim.rlim_max);
            
        printf("File size: soft=%s, hard=%s\n", soft_buf, hard_buf);
    }
    
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0)
    {
        if (rlim.rlim_cur == RLIM_INFINITY)
            strcpy(soft_buf, "unlimited");
        else
            snprintf(soft_buf, sizeof(soft_buf), "%lu", (unsigned long)rlim.rlim_cur);
            
        if (rlim.rlim_max == RLIM_INFINITY)
            strcpy(hard_buf, "unlimited");
        else
            snprintf(hard_buf, sizeof(hard_buf), "%lu", (unsigned long)rlim.rlim_max);
            
        printf("Open files: soft=%s, hard=%s\n", soft_buf, hard_buf);
    }
    
    printf("Note: Resource limits are per-process and do not persist between commands.\n");
    printf("      The limits shown are for the shell process itself, not for commands you run.\n");
}

int set_resource_limit(int resource, rlim_t soft_limit, rlim_t hard_limit)
{
    struct rlimit rlim;
    rlim.rlim_cur = soft_limit;
    rlim.rlim_max = hard_limit;
    
    return setrlimit(resource, &rlim);
}

int handle_rlimit_command(char* command[], int arg_count, FILE* exec_times, int* cmd, 
                         double* total_time, double* last_cmd_time, double* avg_time,
                         double* min_time, double* max_time)
{
    if (arg_count < 2)
        return 0;
    
    if (strcmp(command[1], "show") == 0)
    {
        if (arg_count == 2)
        {
            show_resource_limits();
            return 1;
        }
        else if (arg_count == 3)
        {
            // Show specific resource
            struct rlimit rlim;
            int resource_type = -1;
            
            if (strcmp(command[2], "cpu") == 0)
            {
                resource_type = RLIMIT_CPU;
                if (getrlimit(resource_type, &rlim) == 0)
                {
                    printf("CPU time limits: soft=%s, hard=%s\n",
                          rlim.rlim_cur == RLIM_INFINITY ? "unlimited" : 
                          (rlim.rlim_cur == 0 ? "0s" : "unlimited"),
                          rlim.rlim_max == RLIM_INFINITY ? "unlimited" : 
                          (rlim.rlim_max == 0 ? "0s" : "unlimited"));
                }
            }
            return 1;
        }
        return 0;
    }
    
    if (strcmp(command[1], "set") != 0 || arg_count < 4)
        return 0;
    
    resource_limit_t limits[4]; // max 4 resources
    int limit_count = 0;
    int cmd_start = 2;
    int has_cpu_limit = 0;
    
    // Parse resource limits
    while (cmd_start < arg_count - 1 && limit_count < 4)
    {
        char* eq_sign = strchr(command[cmd_start], '=');
        if (eq_sign == NULL)
            break;
            
        *eq_sign = '\0';
        if (!parse_resource_value(command[cmd_start], eq_sign + 1, &limits[limit_count]))
        {
            printf("ERR: Invalid resource limit format\n");
            return 0;
        }
        
        if (limits[limit_count].resource_type == RLIMIT_CPU)
            has_cpu_limit = 1;
            
        limit_count++;
        cmd_start++;
    }
    
    if (limit_count == 0 || cmd_start >= arg_count)
    {
        printf("ERR: No valid resource limits specified\n");
        return 0;
    }
    
    // Prepare command to execute with limits
    char* new_command[MAX_ARG + 1];
    int new_count = 0;
    
    for (int i = cmd_start; i < arg_count && new_count < MAX_ARG; i++)
    {
        new_command[new_count] = strdup(command[i]);
        if (new_command[new_count] == NULL)
        {
            perror("strdup");
            for (int j = 0; j < new_count; j++)
                free(new_command[j]);
            return 0;
        }
        new_count++;
    }
    new_command[new_count] = NULL;
    
    // Execute command with resource limits
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        for (int i = 0; i < new_count; i++)
            free(new_command[i]);
        return 0;
    }
    
    if (pid == 0)
    {
        // Child process - set limits and execute command
        
        // Set resource limits
        for (int i = 0; i < limit_count; i++)
        {
            struct rlimit rlim;
            rlim.rlim_cur = limits[i].soft_limit;
            rlim.rlim_max = limits[i].hard_limit;
            
            if (setrlimit(limits[i].resource_type, &rlim) != 0)
            {
                perror("setrlimit");
                exit(1);
            }
        }
        
        execvp(new_command[0], new_command);
        perror("execvp");
        exit(1);
    }
    else
    {
        // Parent process
        int status;
        int limit_exceeded = 0;
        double cmd_runtime = 0;
        
        // For CPU intensive tasks, we need to continuously check
        // if we've hit the time limit
        if (has_cpu_limit)
        {
            int done = 0;
            struct timeval current;
            
            while (!done)
            {
                // Non-blocking check
                int result = waitpid(pid, &status, WNOHANG);
                
                if (result == pid) // Process finished
                {
                    done = 1;
                }
                else if (result < 0) // Error
                {
                    perror("waitpid");
                    done = 1;
                }
                else // Still running
                {
                    // Check if we've exceeded CPU limit
                    gettimeofday(&current, NULL);
                    cmd_runtime = (current.tv_sec - start.tv_sec) + 
                                 (current.tv_usec - start.tv_usec) / 1000000.0;
                    
                    if (cmd_runtime >= limits[0].soft_limit) // Assuming CPU is first limit
                    {
                        // Time limit exceeded, kill process
                        kill(pid, SIGTERM);
                        limit_exceeded = 1;
                        
                        // Wait for process to terminate
                        waitpid(pid, &status, 0);
                        done = 1;
                    }
                    
                    // Short sleep to avoid CPU hogging
                    usleep(10000); // 10ms
                }
            }
        }
        else
        {
            // For non-CPU intensive tasks, just wait
            waitpid(pid, &status, 0);
        }
        
        gettimeofday(&end, NULL);
        double runtime = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        
        if (limit_exceeded)
        {
            printf("CPU time limit exceeded!\n");
            // Still count as a command execution
            (*cmd)++;
            *last_cmd_time = limits[0].soft_limit; // Use the limit as the time
            *total_time += *last_cmd_time;
            *avg_time = *total_time / *cmd;
            
            if (*last_cmd_time > *max_time)
                *max_time = *last_cmd_time;
                
            if (*last_cmd_time < *min_time || *min_time == 0)
                *min_time = *last_cmd_time;
                
            fprintf(exec_times, "%s : %.5f sec (terminated due to CPU limit)\n", 
                   command[cmd_start], *last_cmd_time);
            fflush(exec_times);
        }
        // Check how process ended
        else if (WIFEXITED(status))
        {
            int exit_code = WEXITSTATUS(status);
            if (exit_code == 0)
            {
                // Command succeeded
                fprintf(exec_times, "%s : %.5f sec\n", command[cmd_start], runtime);
                fflush(exec_times);
                (*cmd)++;
                *total_time += runtime;
                *last_cmd_time = runtime;
                *avg_time = *total_time / *cmd;
                
                if (runtime > *max_time)
                    *max_time = runtime;
                    
                if (runtime < *min_time || *min_time == 0)
                    *min_time = runtime;
            }
            else
            {
                printf("Command failed with exit code: %d\n", exit_code);
            }
        }
        else if (WIFSIGNALED(status))
        {
            int sig = WTERMSIG(status);
            switch (sig)
            {
                case SIGXCPU:
                    printf("CPU time limit exceeded!\n");
                    // Still count as a command execution
                    (*cmd)++;
                    *last_cmd_time = limits[0].soft_limit; // Use the limit as the time
                    *total_time += *last_cmd_time;
                    *avg_time = *total_time / *cmd;
                    
                    if (*last_cmd_time > *max_time)
                        *max_time = *last_cmd_time;
                        
                    if (*last_cmd_time < *min_time || *min_time == 0)
                        *min_time = *last_cmd_time;
                        
                    fprintf(exec_times, "%s : %.5f sec (terminated due to CPU limit)\n", 
                           command[cmd_start], *last_cmd_time);
                    fflush(exec_times);
                    break;
                case SIGSEGV:
                    printf("Memory allocation failed!\n");
                    break;
                case SIGXFSZ:
                    printf("File size limit exceeded!\n");
                    break;
                default:
                    printf("Terminated by signal: %d\n", sig);
            }
        }
        
        // Clean up
        for (int i = 0; i < new_count; i++)
            free(new_command[i]);
    }
    
    return 1;
}

int execute_with_limits(char* command[], char* original_input, FILE* exec_times, resource_limit_t* limits, int limit_count)
{
    // Implementation of execute_with_limits function
    return 0; // Placeholder return, actual implementation needed
}
