#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/resource.h> // for setrlimit and getrlimit
#include <signal.h> // for signal handling
#include <ctype.h> // for isdigit function
#include <errno.h> // for errno - memory and files errors
#include <sys/types.h> // for pid_t
#include <fcntl.h> // for open function
#include <pthread.h> // for pthread_create

#define MAX_SIZE 1025
#define MAX_ARG 7 // command + 6 arguments
#define MAX_DANG 1000

// Resource limit related defines
#define BYTES_IN_KB 1024
#define BYTES_IN_MB (1024 * 1024)
#define BYTES_IN_GB (1024 * 1024 * 1024)

#define MAX_BG_PROCESSES 100

#define MAX_MATRICES 5 // max arg is 7 so 5 matrices

struct matrix
{
    int rows;
    int cols;
    int size;
    int* data;
};

int space_error(char str[]);
void split_string(char* input, char* result[], int* count, int rlimit_flag);
void input_arg_check(int argc);
FILE* open_file(char* filename, char* mode);
int load_dangerous_commands(FILE* dangerous_commands, char* dng_cmds[]);
int split_and_validate(char* input, char* original_input, char* command[], int rlimit_flag);
void free_resources(char* command[], int arg_count, char* dng_cmds[], int dng_count);
int check_dangerous_command(char* original_input, char* command[], int arg_count);
double execute_command(char* command[], char* original_input);
void update_timing_stats(double runtime, const char* command_name);
double handle_pipe(char* input, char* original_input, int pipe_index);
void handle_mytee(char * command[], int right_arg_count);
int handle_rlimit(char* command[], int arg_count, FILE* exec_times, int* cmd, double* total_time, double* last_cmd_time, double* avg_time, double* min_time, double* max_time);
int set_rlimit(int resource_code, int soft_limit, int hard_limit);
int size_value(const char* value_str);
void handle_sigcpu(int signo);
void handle_sigfsz(int signo);
void handle_sigmem(int signo);
void handle_signof(int signo);
void handle_sigchild(int signo);
int check_process_status(int status, pid_t pid, const char* cmd_name, FILE* exec_file, double runtime, int is_background);
int handle_stderr_redirection(char* command[]); 
void handle_mcalc(char* command[], int arg_count);
int mcalc_format_check(char* command[], int arg_count, struct matrix* matrices[], int* matrix_count);


struct bg_process 
{
    pid_t pid;
    struct timeval start_time;
    char command[MAX_SIZE];
};

struct bg_process bg_processes[MAX_BG_PROCESSES];
int bg_count = 0;
FILE* global_exec_times = NULL; 

struct rlimit rl;

int cmd = 0;
int dangerous_cmd_blocked = 0;
int dangerous_cmd_warning = 0;
char* dng_cmds[MAX_DANG];  // Global array for dangerous commands
int dng_count = 0;         // Global counter for dangerous commands

double last_cmd_time = 0;
double total_time = 0;
double avg_time = 0;
double min_time = 0;
double max_time = 0;

int main(int argc, char* argv[])
{
    //signal handlers
    signal(SIGXCPU  , handle_sigcpu); // cpu
    signal(SIGXFSZ , handle_sigfsz); // files
    signal(SIGSEGV, handle_sigmem); // memory
    signal(SIGUSR1, handle_signof); // open files - using SIGUSR1 as a custom signal
    signal(SIGCHLD, handle_sigchild); //SIGCHLD handler for background processes

    //check for having two files as input
    input_arg_check(argc);

    //open files
    FILE* dangerous_commands = open_file(argv[1], "r");
    FILE* exec_times = open_file(argv[2], "a");
    global_exec_times = exec_times; // assigns a local pointer to the global pointer

    //load dangerous commands
    dng_count = load_dangerous_commands(dangerous_commands, dng_cmds);

    fclose(dangerous_commands);

    // the mini-shell
    while (1) 
    {
        printf("#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:%.5f|avg_time:%.5f|min_time:%.5f|max_time:%.5f>>"
                ,cmd,dangerous_cmd_blocked,last_cmd_time,avg_time,min_time,max_time);

        char input[MAX_SIZE]; //input string
        char original_input[MAX_SIZE]; //to have the original after using splitting
        char* command[MAX_ARG + 1]; //array for arguments + NULL
        char* rlimit_command[MAX_ARG + 7]; //array for rlimit commands + NULL

        //getting input
        if (fgets(input, MAX_SIZE, stdin) == NULL) 
        {
            free_resources(command,MAX_ARG, dng_cmds, dng_count);
            break;
        }

        input[strcspn(input, "\n")] = 0; //remove newline character after using fgets and avoiding execvp error
        strcpy(original_input, input);

        //check for rlimit
        int rlimit_set_flag = 0;
        if (strncmp(input, "rlimit set", 10) == 0)
            rlimit_set_flag = 1;

        //check for pipe
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

         //handle pipe
        if (flag_pipe == 1)
        {
            double runtime = handle_pipe(input, original_input, pipe_index);

            //not using the timing fucntion because it has special timing for pipe
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

            continue; // Skip the regular command processing
        }
        
        //sending input to validation and splitting
        int arg_count;
        if (rlimit_set_flag == 1)
        {
            arg_count = split_and_validate(input, original_input, rlimit_command, rlimit_set_flag);
        }
        else
        {
            arg_count = split_and_validate(input, original_input, command,0);
        }


        // Error in parsing input
        if (arg_count == -1) 
        {
            for (int i = 0; i < MAX_ARG + 1; i++) {
                command[i] = NULL;
            }
            continue;
        }

        if (arg_count == 0) // skip empty input lines
            continue;

        // Handle rlimit command                   
        if (strncmp(input, "rlimit", 6) == 0)
        {
            char** cmd_array = rlimit_set_flag ? rlimit_command : command;
            handle_rlimit(cmd_array, arg_count, exec_times, &cmd, &total_time, &last_cmd_time, &avg_time, &min_time, &max_time);
            
            // Cleanup code
            for (int i = 0; i < arg_count; i++)
            {
                if (cmd_array[i] != NULL)
                    free(cmd_array[i]);
            }
            continue; 
        }


        if (strcmp(command[0], "mcalc") == 0)
        {
            handle_mcalc(command, arg_count);
            continue;
        }


        if (strcmp(command[0], "done") == 0) //checking for done - end of terminal
        {
            printf("%d\n", dangerous_cmd_blocked);

            // Free all resources
            free_resources(command, arg_count, dng_cmds, dng_count);

            fclose(exec_times);
            exit(0);
        }

        int danger_status = check_dangerous_command(original_input, command, arg_count);

        if (danger_status == 1) // Dangerous command detected
        {
            continue;
        }

        double runtime = execute_command(command, original_input);

        if (runtime >= 0) // Command executed successfully
        {
            update_timing_stats(runtime, original_input);
        }

        for (int i = 0; i < arg_count; i++) 
        {
            if (command[i] != NULL) {
                free(command[i]);
                command[i] = NULL; // Set to NULL after freeing
            }
        }
    }

    fclose(exec_times);
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
        if (errno == EMFILE) {  // Too many open files
            raise(SIGUSR1);  // Raise our custom signal
            return NULL; //return NULL to indicate error 
        }
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
            if (errno == ENOMEM) {  // Out of memory
                raise(SIGSEGV);  // Raise memory error signal
                return dng_count;
            }
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

int split_and_validate(char* input, char* original_input, char* command[], int rlimit_flag)
{
    int arg_count = 0;
    int error = 0;  // Track if we have any errors
    int max_arg = MAX_ARG;

    if (rlimit_flag == 1)
        max_arg = MAX_ARG + 6; // 6 for rlimit commands + 7 for the new command

    int space_err = space_error(input);
    if (space_err == 1) //check for one or more spaces
    {
        printf("ERR_SPACE\n");
        error = 1;
    }

    split_string(input, command, &arg_count, rlimit_flag);

    // Check if split_string encountered too many arguments
    if (arg_count == -1 || arg_count > max_arg) 
    {
        printf("ERR_ARGS\n");
        error = 1;
    }

    if (error) {
       
        int rlimit_free = rlimit_flag ? (MAX_ARG + 6) : MAX_ARG;
        free_resources(command, rlimit_free, NULL, 0);
        return -1;
    }

    return arg_count;
}

void free_resources(char* command[], int arg_count, char* dng_cmds[], int dng_count)
{
    // Free command array if it exists
    if (command != NULL) {
        
        int max_to_free = (arg_count < 0) ? MAX_ARG : arg_count;
        for (int i = 0; i < max_to_free && command[i] != NULL; i++)
        {
            free(command[i]);
            command[i] = NULL;
        }
    }
    
    // Free dangerous commands array if it exists
    if (dng_cmds != NULL && dng_count > 0) {
        for (int i = 0; i < dng_count && dng_cmds[i] != NULL; i++)
        {
            free(dng_cmds[i]);
            dng_cmds[i] = NULL;
        }
    }
}

int check_dangerous_command(char* original_input, char* command[], int arg_count)
{
    int warning = 0;
    int dang_err = 0;
    char print_err[MAX_SIZE];  

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
        dangerous_cmd_blocked++;
        free_resources(command, arg_count, NULL, 0);
        return 1;  // Dangerous command
    }
    if (warning)
    {
        printf("WARNING: Command similar to dangerous command (\"%s\"). Proceed with caution.\n", print_err);
        dangerous_cmd_warning++; // Increment the warning counter
        return 2;  // Warning
    }

    return 0;  // No danger
}

double execute_command(char* command[], char* original_input)
{
    pid_t pid;
    int background = 0;
    
    // Check if command should run in background
    int last_arg = 0;
    while (command[last_arg] != NULL) last_arg++;
    
    // Check for & in the last argument
    if (last_arg > 0) {
        char* last_arg_str = command[last_arg-1];
        int len = strlen(last_arg_str);
        if (len > 0 && last_arg_str[len-1] == '&') {
            background = 1;
            // Remove & from the end of the argument
            last_arg_str[len-1] = '\0';
            // If the argument is now empty after removing &, remove it entirely
            if (len == 1) { // This means the argument was solely "&"
                free(command[last_arg-1]); // Free the malloc'd "&" string
                command[last_arg-1] = NULL;
            }
        }
    }

    pid = fork();
    if (pid < 0)
    {
        perror("fork");
        exit(1);
    }
    else if (pid == 0)
    {
        // Check and handle stderr redirection if present
        if (!handle_stderr_redirection(command)) {
            exit(1);
        }

        execvp(command[0], command);
        perror("execvp");
        exit(1);
    }
    else
    {
        struct timeval start, end;
        gettimeofday(&start, NULL);

        if (!background) {
            // For foreground processes, wait normally
            int status;
            waitpid(pid, &status, 0);
            gettimeofday(&end, NULL);
            double runtime = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

            if (check_process_status(status, pid, original_input, global_exec_times, runtime, 0)) {
                return runtime;
            }
            return -1;
        } else {
            // For background processes, store start time and return immediately
            if (bg_count < MAX_BG_PROCESSES) {
                bg_processes[bg_count].pid = pid;
                bg_processes[bg_count].start_time = start;
                strncpy(bg_processes[bg_count].command, original_input, MAX_SIZE - 1);
                bg_processes[bg_count].command[MAX_SIZE - 1] = '\0';
                bg_count++;
            }

            return 0; // Return 0 to indicate background process started
        }
    }
    return -1;
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

void split_string(char* input, char* result[], int* count, int rlimit_flag) {
    *count = 0;
    int word_start = 0;
    int input_length = strlen(input);
    int max_arg = rlimit_flag ? MAX_ARG + 6 : MAX_ARG;
    
    // Initialize all pointers to NULL
    for (int i = 0; i <= max_arg; i++) {
        result[i] = NULL;
    }
    
    for(int i = 0; i <= input_length && *count < max_arg; i++) {
        if(input[i] == ' ' || input[i] == '\0') {
            if(i > word_start) {
                int word_length = i - word_start;
                result[*count] = malloc(word_length + 1);
                if (result[*count] == NULL) {
                    if (errno == ENOMEM) {  // Out of memory
                        raise(SIGSEGV);  // Raise memory error signal
                        // Clean up already allocated memory
                        for (int j = 0; j < *count; j++) {
                            free(result[j]);
                            result[j] = NULL;
                        }
                        *count = -1;  // Signal error
                        return;
                    }
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

double handle_pipe(char* input, char* original_input, int pipe_index)
{
    char input_left[MAX_SIZE];
    char input_right[MAX_SIZE];
    struct timeval start, end;
    double runtime = 0;

    strncpy(input_left, input, pipe_index-1);//splitting to left
    input_left[pipe_index-1] = '\0';

    int right_start = pipe_index + 2; // +2 to skip the the first space
    strcpy(input_right, &input[right_start]); //splitting to right

    char* left_command[MAX_ARG + 1];
    char* right_command[MAX_ARG + 1];

    // Initialize command arrays to NULL
    for (int i = 0; i <= MAX_ARG; i++) {
        left_command[i] = NULL;
        right_command[i] = NULL;
    }

    int left_arg_count = split_and_validate(input_left, original_input, left_command, 0);
    int right_arg_count = split_and_validate(input_right, original_input, right_command, 0);

    if (left_arg_count == -1 || right_arg_count == -1)
    {
        free_resources(left_command, left_arg_count, NULL, 0);
        free_resources(right_command, right_arg_count, NULL, 0);
        return -1;
    }

    if (left_arg_count == 0 || right_arg_count == 0)
    {
        free_resources(left_command, left_arg_count, NULL, 0);
        free_resources(right_command, right_arg_count, NULL, 0);
        return -1;
    }

    // Check if either command is dangerous
    int danger_status_left = check_dangerous_command(input_left, left_command, left_arg_count);

    if (danger_status_left == 1) // Dangerous left command detected
    {
        free_resources(left_command, left_arg_count, NULL, 0);
        free_resources(right_command, right_arg_count, NULL, 0);
        return -1;
    }

    int danger_status_right = check_dangerous_command(input_right, right_command, right_arg_count);

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
        free_resources(left_command, left_arg_count, NULL, 0);
        free_resources(right_command, right_arg_count, NULL, 0);
        exit(1);
    }

    // Start timing for the entire pipe operation
    gettimeofday(&start, NULL);

    pid_t pid_left = fork();
    if (pid_left < 0)
    {
        perror("fork");
        free_resources(left_command, left_arg_count, NULL, 0);
        free_resources(right_command, right_arg_count, NULL, 0);
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
            free_resources(left_command, left_arg_count, NULL, 0);
            free_resources(right_command, right_arg_count, NULL, 0);
            exit(1);
        }
        else if (pid_right == 0)
        {
            close(pipe_fd[1]); //closing the write end of the pipe
            dup2(pipe_fd[0], STDIN_FILENO); //redirecting stdin to the read end of the pipe
            close(pipe_fd[0]); //closing the read end of the pipe now that he was redirected

            if (strcmp(right_command[0], "my_tee") == 0 || strcmp(right_command[0], "tee_my") == 0)
            {
                if (right_arg_count < 2)
                {
                    fprintf(stderr, "ERR: my_tee requires at least one output file\n");
                    exit(1);
                }
                handle_mytee(right_command, right_arg_count);
                exit(0);
            }

            if (!handle_stderr_redirection(right_command)) {
                exit(1);
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

            // Check status of right command
            int right_success = check_process_status(status2, pid_right, input_right, global_exec_times, runtime, 0);

            free_resources(left_command, left_arg_count, NULL, 0);
            free_resources(right_command, right_arg_count, NULL, 0);

            // Return the runtime if both commands succeeded
            if (right_success) {
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

    // Read from stdin
    while (fgets(buffer, sizeof(buffer), stdin) != NULL)
    {
        // Write to stdout
        printf("%s", buffer);

        // Write to each file
        for (int i = start_index; i < right_arg_count; i++) {
            FILE *file = open_file(command[i], append_mode ? "a" : "w");
            if (file) {
                fputs(buffer, file);
                fclose(file);
            } else {
                fprintf(stderr, "Error opening file: %s\n", command[i]);
            }
        }
    }
}

int set_rlimit(int resource_code, int soft_limit, int hard_limit)
{
    struct rlimit rl;
    rl.rlim_cur = soft_limit;
    rl.rlim_max = hard_limit;

    //return 0 if successful, -1 if not
    return setrlimit(resource_code, &rl);
}

int size_value(const char* value_str)
{
    int value = atoi(value_str);
    int len = strlen(value_str);
    
    // Find where the numeric part ends
    int i;
    for (i = 0; i < len; i++) {
        if (!isdigit(value_str[i])) {
            break;
        }
    }
    
    // If the entire string is numeric, return the value as is
    if (i == len) {
        return value;
    }
    
    // Get the unit part
    const char* unit = &value_str[i];
    
    // Convert based on unit
    if (strcmp(unit, "B") == 0) {
        return value; // Bytes
    } else if (strcmp(unit, "K") == 0 || strcmp(unit, "KB") == 0) {
        return value * BYTES_IN_KB; // Kilobytes
    } else if (strcmp(unit, "M") == 0 || strcmp(unit, "MB") == 0) {
        return value * BYTES_IN_MB; // Megabytes
    } else if (strcmp(unit, "G") == 0 || strcmp(unit, "GB") == 0) {
        return value * BYTES_IN_GB; // Gigabytes
    }
    
    // If no valid unit, return the value as is
    return value;
}


int handle_rlimit(char* command[], int arg_count, FILE* exec_times, int* cmd, double* total_time, double* last_cmd_time, double* avg_time, double* min_time, double* max_time)
{
    if (arg_count < 2)
        return 0;

    int resource_code; //0 for cpu, 1 for memory, 2 for size, 3 for files

    if (strcmp(command[1], "show") == 0)
    {
        // Check for exact argument count - should be exactly 2 ("rlimit" and "show")
        if (arg_count != 2) 
        {
            return 0;
        }

        struct timeval start, end;
        gettimeofday(&start, NULL);

        struct rlimit cpu_rl, mem_rl,fsize_rl,files_rl;
        
        // Get CPU limits
        getrlimit(RLIMIT_CPU, &cpu_rl);
        
        // Get memory limits
        getrlimit(RLIMIT_AS, &mem_rl);

        // Get size limits
        getrlimit(RLIMIT_FSIZE, &fsize_rl);
        
        // Get open files limits
        getrlimit(RLIMIT_NOFILE, &files_rl);

        // Print the limits
        
        if (cpu_rl.rlim_cur == RLIM_INFINITY) //cpu limit
            printf("CPU time: soft=unlimited, hard=unlimited\n");
        else
            printf("CPU time: soft=%lus, hard=%lus\n", (unsigned long)cpu_rl.rlim_cur, (unsigned long)cpu_rl.rlim_max);

        if (mem_rl.rlim_cur == RLIM_INFINITY) //memory limit
            printf("Memory: soft=unlimited, hard=unlimited\n");
        else
            printf("Memory: soft=%lu, hard=%lu\n", (unsigned long)mem_rl.rlim_cur, (unsigned long)mem_rl.rlim_max);

        if (fsize_rl.rlim_cur == RLIM_INFINITY) //fsize limit
            printf("File size: soft=unlimited, hard=unlimited\n");
        else
            printf("File size: soft=%lu, hard=%lu\n", (unsigned long)fsize_rl.rlim_cur, (unsigned long)fsize_rl.rlim_max);
            
        if (files_rl.rlim_cur == RLIM_INFINITY) //open files limit
            printf("Open files: soft=unlimited, hard=unlimited\n");
        else
            printf("Open files: soft=%lu, hard=%lu\n", (unsigned long)files_rl.rlim_cur, (unsigned long)files_rl.rlim_max);

        gettimeofday(&end, NULL);
        double runtime = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

        // Update timing statistics using the new function
        update_timing_stats(runtime, "rlimit show");

        for (int i = 0; command[i] != NULL; i++) {
        free(command[i]);
        command[i] = NULL;
    }

        return 1;
    }
    
    if (strcmp(command[1], "set") == 0)
    {
        if (arg_count < 3)
        {
            printf("ERR\n");
            return 0;
        }

        // Find where the actual command starts after the resource limits
        int cmd_start = 2;
        for (; cmd_start < arg_count; cmd_start++) {
            if (strchr(command[cmd_start], '=') == NULL) {
                break;
            }
        }

        // If no command after limits, return error
        if (cmd_start >= arg_count) {
            printf("ERR\n");  // No command provided after limits
            return 0;
        }

        // If we have a command, fork and run it with the new limits
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 0;
        }
        else if (pid == 0) {
            // Child process - set up signal handlers first
            signal(SIGXCPU, handle_sigcpu);
            signal(SIGXFSZ, handle_sigfsz);
            signal(SIGSEGV, handle_sigmem);
            signal(SIGUSR1, handle_signof);

            for (int i = 2; i < cmd_start; i++)
            {
                char resource_name[MAX_SIZE];
                char value_str[MAX_SIZE];
                int hard_limit;
                int soft_limit;

                if (strchr(command[i], '=') != NULL)
                {
                    strncpy(resource_name, command[i], strchr(command[i], '=') - command[i]);
                    resource_name[strchr(command[i], '=') - command[i]] = '\0';
                    strcpy(value_str, strchr(command[i], '=') + 1);

                    if (strchr(value_str, ':') != NULL)
                    {
                        char temp[MAX_SIZE];
                        strncpy(temp, value_str, strchr(value_str, ':') - value_str);
                        temp[strchr(value_str, ':') - value_str] = '\0';
                        soft_limit = size_value(temp);
                        hard_limit = size_value(strchr(value_str, ':') + 1);
                    }
                    else
                    {
                        soft_limit = size_value(value_str);
                        hard_limit = size_value(value_str);
                    }

                    if (strcmp(resource_name, "cpu") == 0)
                        resource_code = RLIMIT_CPU;
                    else if (strcmp(resource_name, "mem") == 0)
                        resource_code = RLIMIT_AS;
                    else if (strcmp(resource_name, "fsize") == 0)
                        resource_code = RLIMIT_FSIZE;
                    else if (strcmp(resource_name, "nofile") == 0)
                        resource_code = RLIMIT_NOFILE;
                    else
                    {
                        fprintf(stderr, "ERR: Not a valid resource\n");
                        exit(1);
                    }

                    if(set_rlimit(resource_code, soft_limit, hard_limit) == -1) 
                    {
                        perror("setrlimit");
                        exit(1);
                    }
                }
            }

            // Create new array for the command
            char* new_command[MAX_ARG + 1];
            int new_index = 0;
            
            // Initialize new_command array to NULL
            for (int i = 0; i <= MAX_ARG; i++) {
                new_command[i] = NULL;
            }
            
            // Copy the command and its arguments
            for (int i = cmd_start; i < arg_count; i++) {
                new_command[new_index++] = command[i];
            }
            new_command[new_index] = NULL;

            check_dangerous_command(new_command[0], new_command, arg_count);
            if (!handle_stderr_redirection(new_command)) {
                exit(1);
            }

            execvp(new_command[0], new_command);
            perror("execvp");
            exit(1);
        }
        else {
            // Parent process
            int status;
            struct timeval start, end;
            gettimeofday(&start, NULL);
            wait(&status);
            gettimeofday(&end, NULL);
            double runtime = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

            if (check_process_status(status, pid, command[cmd_start], exec_times, runtime, 0)) {
                // Update timing statistics using the new function
                update_timing_stats(runtime, command[cmd_start]);
            }

            for (int i = 0; i < arg_count; i++)
            {
            free(command[i]);
            command[i] = NULL;
            }
        }

        return 1;
    }
    return 0;
}

void handle_sigcpu(int signo)
{
    printf("CPU time limit exceeded!\n");
    exit(1);
}

void handle_sigfsz(int signo)
{
    printf("File size limit exceeded!\n");
    exit(1);
}

void handle_sigmem(int signo)
{
    printf("Memory allocation failed!\n");
    exit(1);
}

void handle_signof(int signo)
{
    printf("Too many open files!\n");
    exit(1);
}

void handle_sigchild(int signo) {
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Find the background process in our array
        for (int i = 0; i < bg_count; i++) {
            if (bg_processes[i].pid == pid) {
                // Calculate runtime
                struct timeval end;
                gettimeofday(&end, NULL);
                double runtime = (end.tv_sec - bg_processes[i].start_time.tv_sec) + 
                               (end.tv_usec - bg_processes[i].start_time.tv_usec) / 1000000.0;
                
                // Check process status and handle accordingly
                int success = check_process_status(status, pid, bg_processes[i].command, global_exec_times, runtime, 1);
                
                if (success) {
                    // Success case - update stats
                    update_timing_stats(runtime, bg_processes[i].command);
                }
                
                // Print the prompt with updated or unchanged stats
                printf("\n#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:%.5f|avg_time:%.5f|min_time:%.5f|max_time:%.5f>>",
                      cmd, dangerous_cmd_blocked, last_cmd_time, avg_time, min_time, max_time);
                fflush(stdout);
                
                // Remove the completed process from our array
                for (int j = i; j < bg_count - 1; j++) {
                    bg_processes[j] = bg_processes[j + 1];
                }
                bg_count--;
                break;
            }
        }
    }
}

// Function to handle time measurements and update statistics
void update_timing_stats(double runtime, const char* command_name)
{
    cmd++;
    last_cmd_time = runtime;
    total_time += runtime;
    avg_time = total_time / cmd;

    if (runtime > max_time)
        max_time = runtime;

    if (runtime < min_time || min_time == 0)
        min_time = runtime;

    // Write to exec_times file
    fprintf(global_exec_times, "%s : %.5f sec\n", command_name, runtime);
    fflush(global_exec_times);
}

int check_process_status(int status, pid_t pid, const char* cmd_name, FILE* exec_file, double runtime, int is_background)
{
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 0) {
            return 1; // Success
        } else {
            if (is_background) {
                printf("\nBackground process [%d] failed: %s with exit code %d\n", pid, cmd_name, exit_code);
                if (exec_file != NULL) {
                    fprintf(exec_file, "%s : failed with exit code %d (background)\n", cmd_name, exit_code);
                    fflush(exec_file);
                }
            } else {
                printf("Error: Command '%s' exited with code %d\n", cmd_name, exit_code);
            }
            return 0; // Failure
        }
    } else if (WIFSIGNALED(status)) {
        int signal_num = WTERMSIG(status);
        
        if (is_background) {
            printf("\nBackground process [%d] ", pid);
        } 
        
        // Print signal name based on number
        switch (signal_num) {
            case SIGSEGV: printf("Memory allocation failed!"); break;
            case SIGINT: printf("terminated by signal: SIGINT"); break;
            case SIGTERM: printf("terminated by signal: SIGTERM"); break;
            case SIGKILL: printf("terminated by signal: SIGKILL"); break;
            case SIGABRT: printf("terminated by signal: SIGABRT"); break;
            case SIGFPE: printf("terminated by signal: SIGFPE"); break;
            case SIGILL: printf("terminated by signal: SIGILL"); break;
            case SIGPIPE: printf("terminated by signal: SIGPIPE"); break;
            case SIGQUIT: printf("terminated by signal: SIGQUIT"); break;
            case SIGTRAP: printf("terminated by signal: SIGTRAP"); break;
            case SIGXCPU: printf("CPU time limit exceeded!"); break;
            case SIGXFSZ: printf("File size limit exceeded!"); break;
            case SIGUSR1: printf("Too many open files!"); break;
            default: printf("terminated by signal: %d", signal_num);
        }
        
        if (is_background) {
            printf(" - %s\n", cmd_name);
            if (exec_file != NULL) {
                fprintf(exec_file, "%s : terminated by signal %d (background)\n", cmd_name, signal_num);
                fflush(exec_file);
            }
        } else {
            printf("\n");
        }
        
        return 0; // Failure
    }
    return 0; // Failure
}

int handle_stderr_redirection(char* command[]) {
    char* stderr_file = NULL;
    
    // Check for stderr redirection
    for (int i = 0; command[i] != NULL; i++) {
        if (strcmp(command[i], "2>") == 0) {
            if (command[i+1] != NULL) {
                stderr_file = command[i+1];
                // Remove the 2> and filename from command
                for (int j = i; command[j] != NULL; j++) {
                    if (command[j+2] != NULL) {
                        command[j] = command[j+2];
                    } else {
                        command[j] = NULL;
                        break;
                    }
                }
                
                // Found 2>, now handle the redirection
                int fd = open(stderr_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd == -1) {
                    perror("open");
                    return 0;
                }
                if (dup2(fd, STDERR_FILENO) == -1) {
                    perror("dup2");
                    close(fd);
                    return 0;
                }
                close(fd);
                return 1;
            }
        }
    }
    return 1; // No redirection found, return success
}

int mcalc_format_check(char* command[], int arg_count, struct matrix* matrices[], int* matrix_count)
{
    if (arg_count < 4) // must have mcalc, at least two matrices, and operation
    {
        return 1;
    }

    // check if operation is valid
    if (strcmp(command[arg_count-1], "\"ADD\"") != 0 && strcmp(command[arg_count-1], "\"SUB\"") != 0)
    {
        return 1;
    }

     // Variables to track dimensions of the first matrix for comparison
    int first_rows = 0;
    int first_cols = 0;

    *matrix_count = 0;

    // Check each matrix
    for (int i = 1; i < arg_count - 1; i++)
    {
        char* arg = command[i];
        int arg_len = strlen(arg);
        
        // check if matrix is in quotes and ()
        if (arg_len < 7 || arg[0] != '"' || arg[1] != '(' || 
            arg[arg_len-2] != ')' || arg[arg_len-1] != '"')
        {
            return 1;
        }

        // Create a temporary copy without the outer quotes and parentheses
        char arg_copy[MAX_SIZE];
        strncpy(arg_copy, arg + 2, arg_len - 4);  // Skip first two chars and last two chars
        arg_copy[arg_len - 4] = '\0';
        
        // Find colon separator between dimensions and data
        char* colon = strchr(arg_copy, ':');
        if (!colon) 
        {
            return 1;
        }
        
        // Parse dimensions
        *colon = '\0'; // Temporarily terminate string at colon for dimension parsing
        
        char* dims = arg_copy;
        char* comma = strchr(dims, ',');
        if (!comma) {
            return 1;
        }
        
        // Get rows and columns
        *comma = '\0';
        int rows = atoi(dims);
        int cols = atoi(comma + 1);
        if (rows <= 0 || cols <= 0) {
            return 1;
        }

    

         // Save dimensions of first matrix to compare with others
        if (i == 1) {
            first_rows = rows;
            first_cols = cols;
        }
        // Check that current matrix has same dimensions as first matrix
        else if (rows != first_rows || cols != first_cols) {
            return 1;  // Matrix dimensions don't match
        }
        
        // Restore the colon 
        *colon = ':';

        // Allocate memory for the new matrix
        matrices[*matrix_count] = (struct matrix*)malloc(sizeof(struct matrix));
        if (matrices[*matrix_count] == NULL) {
            return 1;  // Memory allocation failed
        }
        
        matrices[*matrix_count]->rows = rows;
        matrices[*matrix_count]->cols = cols;
        matrices[*matrix_count]->size = rows * cols;
        matrices[*matrix_count]->data = (int*)malloc(rows * cols * sizeof(int));
        
        if (matrices[*matrix_count]->data == NULL) {
            free(matrices[*matrix_count]);
            return 1;  // Memory allocation failed
        }
        
        // Count elements in data and check if it is a number
        int count = 0;
        char* data = colon + 1;
        char* p = data;
        int in_number = 0;
        
        while (*p) {
            if (isdigit(*p) || *p == '-')//check for number and negative
            {
                if (!in_number) {
                    count++;
                    in_number = 1;
                }
            } else {
                in_number = 0;
            }
            p++;
        }

        // insert data into matrix
        int data_index = 0;
        char *data_str = colon + 1;        // points to the first digit after “:”
        char *token = strtok(data_str, ",)"); 
        while (token != NULL) {
            if (data_index < rows * cols) {
                matrices[*matrix_count]->data[data_index++] = atoi(token);
            }
            token = strtok(NULL, ",)");
        }
        
        // Verify element count matches dimensions
        if (count != rows * cols)
        {
            free(matrices[*matrix_count]->data);
            free(matrices[*matrix_count]);
            return 1;
        }

        (*matrix_count)++;
    }

    
    return 0;
}

void handle_mcalc(char* command[], int arg_count)
{
    // Create matrices from command arguments
    int matrix_count = 0;
    struct matrix* matrices[MAX_MATRICES];
    
    int error = mcalc_format_check(command, arg_count, matrices, &matrix_count);
    if (error)
    {
        printf("ERR_MAT_INPUT\n");
        return;
    }

    

    for (int i = 0; i < matrix_count; i++)
    {
        printf("Matrix %d:\n", i + 1);
        printf("Rows: %d, Columns: %d\n", matrices[i]->rows, matrices[i]->cols);
        printf("Data: ");
        for (int j = 0; j < matrices[i]->size; j++) {
            printf("%d ", matrices[i]->data[j]);
        }
        printf("\n");
    }

}
