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

#define MAX_SIZE 1025
#define MAX_ARG 7 // command + 6 arguments
#define MAX_DANG 1000

// Resource limit related defines
#define BYTES_IN_KB 1024
#define BYTES_IN_MB (1024 * 1024)
#define BYTES_IN_GB (1024 * 1024 * 1024)

int space_error(char str[]);
void split_string(char* input, char* result[], int* count, int rlimit_flag);
void input_arg_check(int argc);
FILE* open_file(char* filename, char* mode);
int load_dangerous_commands(FILE* dangerous_commands, char* dng_cmds[]);
int split_and_validate(char* input, char* original_input, char* command[], int rlimit_flag);
void free_resources(char* command[], int arg_count, char* dng_cmds[], int dng_count);
int check_dangerous_command(char* original_input, char* command[], char* dng_cmds[], int dng_count, char* print_err, int* dangerous_cmd_warning, int* dangerous_cmd_blocked, int arg_count);
double execute_command(char* command[], char* original_input, FILE* exec_times);
double handle_pipe(char* input, char* original_input, FILE* exec_times, char* dng_cmds[], int dng_count, int pipe_index, int* dangerous_cmd_warning, int* dangerous_cmd_blocked);
void handle_mytee(char * command[], int right_arg_count);
int handle_rlimit(char* command[], int arg_count, FILE* exec_times, int* cmd, double* total_time, double* last_cmd_time, double* avg_time, double* min_time, double* max_time);
int set_rlimit(int resource_code, int soft_limit, int hard_limit);
int size_value(const char* value_str);
void handle_sigcpu(int signo);
void handle_sigfsz(int signo);
void handle_sigmem(int signo);
void handle_signof(int signo);

struct rlimit rl;

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

    signal(SIGXCPU  , handle_sigcpu); // cpu
    signal(SIGXFSZ , handle_sigfsz); // files
    signal(SIGSEGV, handle_sigmem); // memory
    signal(SIGUSR1, handle_signof); // open files - using SIGUSR1 as a custom signal
    


    while (1) // the mini-shell
    {
        printf("#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:%.5f|avg_time:%.5f|min_time:%.5f|max_time:%.5f>>"
                ,cmd,dangerous_cmd_blocked,last_cmd_time,avg_time,min_time,max_time);

        char input[MAX_SIZE]; //input str
        char original_input[MAX_SIZE]; //to have the original after using splitting
        char* command[MAX_ARG + 1]; //array for arguments + NULL
        char* rlimit_command[MAX_ARG + 5]; //array for rlimit commands + NULL

        if (fgets(input, MAX_SIZE, stdin) == NULL) //getting input
        {
            perror("fgets");
            exit(1);
        }

        input[strcspn(input, "\n")] = 0; //remove newline character after using fgets and avoiding execvp error
        strcpy(original_input, input);

        int rlimit_set_flag = 0;
        if (strncmp(input, "rlimit set", 10) == 0)
            rlimit_set_flag = 1;

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

        if (arg_count == -1) // Error in parsing input
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
            if (rlimit_set_flag)
            {
                if (handle_rlimit(rlimit_command, arg_count, exec_times, &cmd, &total_time, &last_cmd_time, &avg_time, &min_time, &max_time))
                {
                    for (int i = 0; i < arg_count; i++)
                    {
                        if (rlimit_command[i] != NULL)
                            free(rlimit_command[i]);
                    }
                    continue;
                }
            }
            else
            {
                if (handle_rlimit(command, arg_count, exec_times, &cmd, &total_time, &last_cmd_time, &avg_time, &min_time, &max_time))
                {
                    for (int i = 0; i < arg_count; i++)
                    {
                        if (command[i] != NULL)
                            free(command[i]);
                    }
                    continue;
                }
            }
        }

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

        double runtime = execute_command(command, original_input, exec_times);

        if (runtime >= 0) // Command executed successfully
        {
            cmd++;
            total_time += runtime;
            last_cmd_time = runtime;
            avg_time = total_time / cmd;

            if (runtime > max_time)
                max_time = runtime;

            if (runtime < min_time || min_time == 0)
            {
                min_time = runtime;
            }
        }

        for (int i = 0; i < arg_count; i++) {
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
        if (errno == EMFILE) {  // Too many open files
            raise(SIGUSR1);  // Raise our custom signal
            return NULL;
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
        max_arg = MAX_ARG + 4; 

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

void split_string(char* input, char* result[], int* count, int rlimit_flag) {
    *count = 0;
    int word_start = 0;
    int input_length = strlen(input);
    int max_arg = rlimit_flag ? MAX_ARG + 4 : MAX_ARG;
    
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

    int left_arg_count = split_and_validate(input_left, original_input, left_command, 0);
    int right_arg_count = split_and_validate(input_right, original_input, right_command, 0);

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

        return 1;
    }
    
    if (strcmp(command[1], "set") == 0)
    {
        if (arg_count < 3)
        {
            printf("ERR\n");
            return 0;
        }

        for (int i = 2; i < arg_count; i++)
        {
            char resource_name[MAX_SIZE];
            char value_str[MAX_SIZE];
            int hard_limit;
            int soft_limit;

            int new_index = 2; //rlimit + set arg

            if (strchr(command[i], '=') != NULL)
            {
                new_index++; //to have the correct index for the new command

                //extracting resource name and value
                strncpy(resource_name, command[i], strchr(command[i], '=') - command[i]);
                resource_name[strchr(command[i], '=') - command[i]] = '\0';
                strcpy(value_str, strchr(command[i], '=') + 1);

                //checking if there are hard and soft limits
                if (strchr(value_str, ':') != NULL)
                {
                    //extracting hard and soft limits
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

                //checking the resource name and setting the correct limit
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
                    return 0;
                }

                if(set_rlimit(resource_code, soft_limit, hard_limit) == -1) 
                {
                    perror("setrlimit");
                    return 0;
                }
                
            }

            //char* new_command[MAX_ARG + 1 - 2]; // -2 because we already have the rlimit and set arg




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
    printf("Memory limit exceeded!\n");
    exit(1);
}

void handle_signof(int signo)
{
    printf("Open files limit exceeded!\n");
    exit(1);
}

