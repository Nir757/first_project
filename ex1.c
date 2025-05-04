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
void check_arguments(int argc);
FILE* open_file(char* filename, char* mode);
int load_dangerous_commands(FILE* dangerous_commands, char* dng_cmds[]);
int parse_input(char* input, char* original_input, char* command[]);
void free_resources(char* command[], int arg_count, char* dng_cmds[], int dng_count);
int check_dangerous_command(char* original_input, char* command[], char* dng_cmds[], int dng_count, char* print_err);
double execute_command(char* command[], char* original_input, FILE* exec_times);

int main(int argc, char* argv[])
{
    check_arguments(argc);

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
        char original_input[MAX_SIZE]; //to have the original after using strtok
        char* command[MAX_ARG + 1]; //array for arguments + NULL

        if (fgets(input, MAX_SIZE, stdin) == NULL) //getting input
        {
            perror("fgets");
            exit(1);
        }

        input[strcspn(input, "\n")] = 0; //remove newline character after using fgets and avoiding execvp error
        strcpy(original_input, input);

        int arg_count = parse_input(input, original_input, command);
        if (arg_count == -1) // Error in parsing input
        {
            continue;
        }

        if (arg_count == 0) // skip empty input lines
            continue;

        command[arg_count] = NULL; // for execvp format

        if (strcmp(command[0], "done") == 0) //checking for done - end of terminal
        {
            printf("%d\n", dangerous_cmd_blocked);
            free_resources(command, arg_count, dng_cmds, dng_count);
            fclose(exec_times);
            exit(0);
        }

        char print_err[MAX_SIZE];
        int danger_status = check_dangerous_command(original_input, command, dng_cmds, dng_count, print_err);

        if (danger_status == 1) // Dangerous command detected
        {
            printf("ERR: Dangerous command detected (\"%s\"). Execution prevented.\n", print_err);
            dangerous_cmd_blocked++;
            for (int i = 0; i < arg_count; i++)
            {
                free(command[i]);
            }
            continue;
        }
        else if (danger_status == 2) // Warning for similar command
        {
            printf("WARNING: Command similar to dangerous command (\"%s\"). Proceed with caution.\n", print_err);
            dangerous_cmd_warning++;
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

void check_arguments(int argc)
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

int parse_input(char* input, char* original_input, char* command[])
{
    int arg_count = 0;
    
    int space_err = space_error(input);
    if (space_err == 1) //check for one or more spaces
    {
        printf("ERR_SPACE\n");
    }

    char* token = strtok(input, " "); //splitting to arguments

    while(token != NULL) //slicing the string and inserting to an array
    {
        command[arg_count] = malloc(strlen(token) + 1);
        if (command[arg_count] == NULL)
        {
            perror("malloc");
            exit(1);
        }

        strcpy(command[arg_count], token);

        arg_count++;
        token = strtok(NULL, " "); //finding the next argument and returning null at the end
    }

    int arg_error = 0;
    if(arg_count > MAX_ARG) //check for argument number
    {
        printf("ERR_ARGS\n");
        arg_error = 1;
    }

    if (arg_error == 1 || space_err == 1)
    {
        for (int i = 0; i < arg_count; i++)
        {
            free(command[i]);
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

int check_dangerous_command(char* original_input, char* command[], char* dng_cmds[], int dng_count, char* print_err)
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
        char* dang_cmd = strtok(original_dang_copy, " ");
        if (dang_cmd != NULL && strcmp(dang_cmd, command[0]) == 0)
        {
            warning = 1;
            strcpy(print_err, dng_cmds[i]);
        }
    }

    if (dang_err)
        return 1;  // Dangerous command
    if (warning)
        return 2;  // Warning
    
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