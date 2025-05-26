#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>

#define MAX_SIZE 1025
#define MAX_ARG 7 // command + 6 arguments
#define MAX_DANG 1000

int space_error(char str []);

int main(int argc,char* argv[])
{
    char line[MAX_SIZE]; // to read from file

    if(argc < 3)
    {
        fprintf(stderr, "Error: please include two files\n");
        exit(1);
    }

    FILE* dangerous_commands = fopen(argv[1],"r");
    if(dangerous_commands == NULL)
    {
        fprintf(stderr, "ERR\n");
        exit(1);
    }

    FILE* exec_times = fopen(argv[2],"a");
    if(exec_times == NULL)
    {
        fprintf(stderr, "ERR\n");
        exit(1);
    }

    char* dng_cmds[MAX_DANG];
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

        char* command[MAX_ARG + 1]; //array for arguments + NULL
        int arg_count = 0;

        if (fgets(input, MAX_SIZE,stdin) == NULL) //getting input
        {
            perror("fgets");
            exit(1);
        }

        input[strcspn(input, "\n")] = 0; //remove newline character after using fgets and avoiding execvp error

        char original_input[MAX_SIZE]; //to have the original after using strtok
        strcpy(original_input,input);

        int space_err = space_error(input);
        if (space_err == 1) //check for one or more spaces
            printf("ERR_SPACE\n");


        char* token = strtok(input," "); //splitting to arguments

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
            continue;
        }


        if (arg_count == 0) // skip empty input lines
            continue;


        command[arg_count] = NULL; // for execvp format

        if (strcmp (command[0], "done") == 0) //checking for done - end of terminal
        {
            printf("%d\n",dangerous_cmd_blocked);

            for (int i = 0; i < arg_count; i++)
            {
                free(command[i]);
            }
            for (int i = 0; i < dng_count; i++)
            {
                free(dng_cmds[i]);
            }
            exit(0);
        }

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
            char* dang_cmd = strtok(original_dang_copy, " ");
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
            for (int i = 0; i < arg_count; i++)
            {
                free(command[i]);
            }
            continue;
        }

        if (warning)
        {
            printf("WARNING: Command similar to dangerous command (\"%s\"). Proceed with caution.\n", print_err);
            dangerous_cmd_warning++;
        }

        pid_t pid;
        pid = fork();
        if (pid < 0)
        {
            perror("fork");
            exit(1);
        }
        else if (pid == 0)
        {
            execvp(command[0],command);
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

                cmd++;

                total_time += runtime;
                last_cmd_time = runtime;
                avg_time = total_time / cmd;

                if (runtime> max_time)
                    max_time = runtime;

                if (runtime < min_time || min_time == 0)
                {
                    min_time = runtime;
                }

                fprintf(exec_times, "%s : %.5f sec\n", original_input,runtime);

                fflush(exec_times);
            }

        }
        for (int i = 0; i < arg_count; i++) {
            free(command[i]);
        }

    }

    return 0;
}

int space_error (char str [])
{
    for (int i = 0; str [i] != '\0'; i++)
    {
        if (str[i] == ' ' && str[i + 1] == ' ')
            return 1;

    }
    return 0;
}