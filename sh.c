#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "./jobs.h"
#define SIZE 1024

// global jobs list
job_list_t *job_list;
int job_count = 1;

/*
 * This function prints to stderr, while error checking and flushing
 *
 * str - the string to be printed
 */
void print_stderr(char *str) {
    if (fprintf(stderr, "%s", str) < 0) {
        fprintf(stderr, "ERROR - printf error.\n");
        cleanup_job_list(job_list);
        exit(EXIT_FAILURE);
    }
    if (fflush(stdout) < 0) {
        fprintf(stderr, "ERROR - fflush error.\n");
        cleanup_job_list(job_list);
        exit(EXIT_FAILURE);
    }
}

/*
 * This function parses an input buffer into tokens, and further processes these
 * tokens into program arguments. This function also accounts for input
 * and output redirection, preparing these redirections for later use.

 * buffer - the raw buffer as received by read()
 * tokens - an array of string pointers, where we put tokens before processing
 * argv - an array of string pointers, where program arguments are stored
 *         (NOTE: we don't trim the path name yet, so that we have access to the
 *                full path in execute())
 * input_path - a pointer to a string pointer, where we'll store
                 the input redirection path if we have one
 * output_path - a pointer to a string pointer, where we'll store
                  the output redirection path if we have one
 * output_type - a pointer to an integer, reflecting the type
                  of output redirection (0 is >, 1 is >>)

 * returns: 0 if we can proceed with execution
 *          1 if an error occurs and we should skip execution
 */
int parse(char buffer[SIZE], char *tokens[512], char *argv[512],
          char **input_path, char **output_path, int *output_type) {
    // --------------------------- defines ---------------------------------
    const char whtspc[6] = " \t";
    char *token = 0;
    /* number of times in/out redirection has occurred
     *  if already at 1 and anotherof the same type is detected, return 1 */
    int in_flag = 0;
    int out_flag = 0;
    /* if skip is set, the previous token was a redirect symbol,
     *  so we don't add it to argv */
    int skip = 0;
    // current argv index number
    int argv_index = 0;
    // output redirection type
    char *type1;

    // -------------------------- get tokens -------------------------------
    int i = 0;
    while ((token = strtok(buffer, whtspc)) != NULL) {
        tokens[i] = token;
        buffer = NULL;
        i++;
    }

    // null-terminate tokens
    tokens[i] = NULL;

    // if no tokens, skip execution
    if (i == 0) {
        return 1;
    }

    // --------------- redirection and assignment to argv -------------------
    int j;
    for (j = 0; j < i; ++j) {
        // check for input redirection
        if (strstr(tokens[j], "<") != NULL) {
            // check if we're at the last index
            if (j == i - 1) {
                print_stderr("ERROR - no redirect file specified.\n");
                return 1;
            }
            /* ensure we haven't already found an input redirect
             *  and the previous character wasn't a redirect character */
            else if (in_flag == 0) {
                if (skip == 1) {
                    print_stderr("ERROR - Two consecutive redirect symbols.\n");
                    return 1;
                }
                // input redirection setup
                *(input_path) = tokens[j + 1];

                // prepare for next iteration
                in_flag = 1;
                skip = 1;
            }
            // otherwise, we have two input redirects
            else {
                print_stderr("syntax error: multiple input files\n");
                return 1;
            }
        }

        // check for output redirection
        else if ((type1 = strstr(tokens[j], ">>")) != NULL ||
                 strstr(tokens[j], ">") != NULL) {
            // check if we're at the last index
            if (j == i - 1) {
                print_stderr("ERROR - no redirect file specified.\n");
                return 1;
            }
            /* ensure we haven't already found an output redirect
             *  and the previous character wasn't a redirect character */
            else if (out_flag == 0) {
                if (skip == 1) {
                    print_stderr("ERROR - Two consecutive redirect symbols.\n");
                    return 1;
                }
                // output redirection setup
                *(output_path) = tokens[j + 1];
                if (type1 == NULL) {
                    *(output_type) = 0;
                } else {
                    *(output_type) = 1;
                }

                // prepare for next iteration
                out_flag = 1;
                skip = 1;
            }
            // otherwise, we have two output redirects
            else {
                print_stderr("syntax error: multiple output files\n");
                return 1;
            }
        }
        // if we're at a token not part of redirection, assign to argv
        else if (skip != 1 && strcmp(tokens[j], "\n") != 0) {
            argv[argv_index] = tokens[j];
            argv_index++;
        } else {
            // prepare for next cycle, skipping over argument to redirect
            skip = 0;
        }
    }

    // ----------------------- post-processing ------------------------------

    // null-terminating argv
    argv[argv_index] = NULL;

    // check for only newline input
    if (strcmp(tokens[0], "\n") == 0) {
        return 1;
    }

    // check for no args
    if (argv[0] == NULL) {
        print_stderr("ERROR - No command.\n");
        return 1;
    }

    // ----------------------- argument trimming ---------------------------
    // trim newline character from last token
    char *ptr;
    if ((ptr = strchr(tokens[i - 1], '\n')) != NULL) {
        *ptr = '\0';
    }
    // trim newline character from last argv
    if ((ptr = strchr(argv[argv_index - 1], '\n')) != NULL) {
        *ptr = '\0';
    }

    // if no errors, we'll proceed with execution
    return 0;
}

/*
 * This function takes an array of arguments, and either runs builtin procedures
 *  (cd, ln, rm, exit) or user-specified executables. User-specified executables
 *  run in child processes, with the capacity for input and output redirection.
 *
 * argv - an array of string pointers, where program arguments are stored
 * input_redir_name - a string, the pathname where we want to pull input from
 * output_redir_name - a string, the pathname where we want to pull output from
 * output_redir_type - an integer, reflecting the type of output
                        redirection (0 is >, 1 is >>)
 */
void execute(char *argv[512], char *input_redir_name, char *output_redir_name,
             int output_redir_type) {
    // --------------------------- defines ---------------------------------
    // path holds the full path name
    size_t length = strlen(argv[0]) + 1;
    char path[length];
    path[length - 1] = '\0';
    strcpy(path, argv[0]);

    int job_id;
    pid_t job_pid;
    pid_t child_pid;
    int is_bg;
    int status;
    int signl;
    int pid;

    // trim path name from first argv
    char *ptr;
    if ((ptr = strrchr(argv[0], '/')) != NULL) {
        argv[0] = &ptr[1];
    }

    // ------------------ check for builtin commands -----------------------
    // cd
    if (strcmp(path, "cd") == 0) {
        if (argv[1] == NULL) {
            print_stderr("cd: syntax error\n");
            return;
        } else if (chdir(argv[1]) < 0) {
            perror("ERROR - Error opening directory");
            return;
        }
        return;
    }
    // ln
    else if (strcmp(path, "ln") == 0) {
        if (argv[1] == NULL || argv[2] == NULL) {
            print_stderr("ln: syntax error\n");
            return;
        } else if (link(argv[1], argv[2]) < 0) {
            perror("ERROR - Error linking file");
            return;
        }
        return;
    }
    // rm
    else if (strcmp(path, "rm") == 0) {
        if (argv[1] == NULL) {
            print_stderr("rm: syntax error\n");
            return;
        } else if (unlink(argv[1]) < 0) {
            perror("ERROR - Error unlinking file");
            return;
        }
        return;
    }
    // exit
    else if (strcmp(path, "exit") == 0) {
        cleanup_job_list(job_list);
        exit(EXIT_SUCCESS);
    }
    // fg
    else if (strcmp(path, "fg") == 0) {
        if (argv[1] == NULL) {
            print_stderr("fg: syntax error\n");
            return;
        }
        char job_id_raw[strlen(argv[1])];
        strcpy(job_id_raw, argv[1]);

        if (job_id_raw[0] != '%') {
            print_stderr("fg: syntax error\n");
            return;
        }

        job_id = atoi(&job_id_raw[1]);

        if ((job_pid = get_job_pid(job_list, job_id)) < 0) {
            print_stderr("ERROR - job not found\n");
            return;
        }

        // give terminal control with the tcgrptset command or whatever it is
        if (tcsetpgrp(STDIN_FILENO, job_pid) < 0) {
            print_stderr("ERROR - error transferring control\n");
            return;
        }

        // attempt to restart process in foreground
        if (kill(-job_pid, SIGCONT) < 0) {
            print_stderr("ERROR - error sending signal\n");
            return;
        }

        update_job_jid(job_list, job_id, RUNNING);

        if ((pid = waitpid(job_pid, &status, WUNTRACED)) < 0) {
            perror("waitpid");
            return;
        }

        // if terminated by signal
        if (WIFSIGNALED(status)) {
            signl = WTERMSIG(status);
            printf("[%d] (%d) terminated by signal %d\n", job_id, pid, signl);
            remove_job_jid(job_list, job_id);
        }

        // if stopped by signal
        if (WIFSTOPPED(status)) {
            signl = WSTOPSIG(status);
            printf("[%d] (%d) suspended by signal %d\n", job_id, pid, signl);
            update_job_jid(job_list, job_id, STOPPED);
        }

        // if exited normally
        if (WIFEXITED(status)) {
            remove_job_jid(job_list, job_id);
        }

        // transfer control to shell
        if (tcsetpgrp(STDIN_FILENO, getpgrp()) < 0) {
            perror("ERROR - Error setting group ID");
        }

        return;
    }
    // bg
    else if (strcmp(path, "bg") == 0) {
        if (argv[1] == NULL) {
            print_stderr("bg: syntax error\n");
            return;
        }
        char job_id_raw[strlen(argv[1])];
        strcpy(job_id_raw, argv[1]);

        if (job_id_raw[0] != '%') {
            print_stderr("bg: syntax error\n");
            return;
        }

        job_id = atoi(&job_id_raw[1]);

        if ((job_pid = get_job_pid(job_list, job_id)) < 0) {
            print_stderr("ERROR - job not found\n");
            return;
        }
        if (kill(-job_pid, SIGCONT) < 0) {
            print_stderr("ERROR - error sending signal\n");
            return;
        }

        update_job_jid(job_list, job_id, RUNNING);
        return;
    }
    // jobs
    else if (strcmp(path, "jobs") == 0) {
        jobs(job_list);
        return;
    }

    // --------------- check for user-defined executables ------------------
    // check to see if the file exists
    if (open(path, O_RDONLY) < 0) {
        perror("ERROR - Error opening file");
        return;
    }

    // ------------------ check for background process --------------------
    int i = 0;
    while (argv[i] != NULL) {
        i++;
    }
    // if starting a background process
    if (strcmp(argv[i - 1], "&") == 0) {
        is_bg = 1;
    } else {
        is_bg = 0;
    }

    // if we're in the child
    if ((child_pid = fork()) == 0) {
        // ------------------------ signal setup ---------------------------
        // signal redirection
        if (setpgid(child_pid, child_pid) < 0) {
            perror("ERROR - Error setting pgid");
            return;
        }

        // ------ check if we need to run process in background -----------
        if (is_bg) {
            argv[i - 1] = '\0';
        }
        // if not in background, update group ID SHOULD BE ELSE IF
        else if (tcsetpgrp(STDIN_FILENO, getpgrp()) < 0) {
            perror("ERROR - Error setting group ID");
            return;
        }

        // restore default signal handling
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);

        // ----------------------- I/O redirection -------------------------
        // input redirection and error checking
        if (input_redir_name != NULL) {
            close(STDIN_FILENO);
            if (open(input_redir_name, O_RDONLY) < 0) {
                perror("ERROR - Error opening file for reading");
                return;
            }
        }
        // output redirection and error checking
        if (output_redir_name != NULL) {
            close(STDOUT_FILENO);
            if (output_redir_type == 0) {
                if (open(output_redir_name, O_WRONLY | O_CREAT | O_TRUNC) < 0) {
                    perror("ERROR - Error opening file for writing");
                    return;
                }
            } else {
                if (open(output_redir_name, O_WRONLY | O_CREAT | O_APPEND) <
                    0) {
                    perror("ERROR - Error opening file for writing");
                    return;
                }
            }
        }

        // execute in the child process
        if (execv(path, argv) < 0) {
            perror("ERROR - Error executing file");
            return;
        }
    }
    // if it was a background process, add it to the job list
    if (is_bg) {
        if (add_job(job_list, job_count, child_pid, RUNNING, path) < 0) {
            print_stderr("ERROR - Error adding job.\n");
            return;
        }
        printf("[%d] (%ld)\n", job_count, (long)child_pid);
        job_count++;
    } else {
        // if foreground process waitpid, otherwise to list
        if ((pid = waitpid(child_pid, &status, WUNTRACED)) < 0) {
            perror("waitpid");
            return;
        }

        // if terminated by signal
        if (WIFSIGNALED(status)) {
            signl = WTERMSIG(status);
            printf("[%d] (%d) terminated by signal %d\n", job_count, pid,
                   signl);
        }
        // if stopped by signal
        if (WIFSTOPPED(status)) {
            signl = WSTOPSIG(status);
            printf("[%d] (%d) suspended by signal %d\n", job_count, pid, signl);
            if (add_job(job_list, job_count, pid, STOPPED, path) < 0) {
                print_stderr("ERROR - Error adding job.\n");
                return;
            }
            job_count++;
        }

        // transfer control to shell
        if (tcsetpgrp(STDIN_FILENO, getpgrp()) < 0) {
            perror("ERROR - Error setting group ID");
        }
    }
    return;
}

void reap() {
    int status;
    int signal;
    int jid;
    int pid;

    // check for termination of background processes
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        // check for waitpid error
        if (pid == -1) {
            perror("waitpid");
            return;
        }
        // get jid
        if ((jid = get_job_jid(job_list, (pid_t)pid)) < 0) {
            print_stderr("ERROR - job not found.\n");
        }

        // -------------------------- check status -------------------------
        // if terminated normally
        if (WIFEXITED(status)) {
            signal = WEXITSTATUS(status);
            printf("[%d] (%d) terminated with exit status %d\n", jid, pid,
                   signal);
            remove_job_jid(job_list, jid);
        }
        // if terminated by signal
        if (WIFSIGNALED(status)) {
            signal = WTERMSIG(status);
            printf("[%d] (%d) terminated by signal %d\n", jid, pid, signal);
            remove_job_jid(job_list, jid);
        }
        // if stopped by signal
        if (WIFSTOPPED(status)) {
            signal = WSTOPSIG(status);
            printf("[%d] (%d) suspended by signal %d\n", jid, pid, signal);
            update_job_jid(job_list, jid, STOPPED);
        }
        // if continued by signal
        if (WIFCONTINUED(status)) {
            printf("[%d] (%d) resumed\n", jid, pid);
            update_job_jid(job_list, jid, RUNNING);
        }
    }
    return;
}

int main() {
    /* main outline:
      - enter repl
      - check if we've got EOF (Ctrl + D), if so break
      - get input with parse
      - execute input
      - await next line of input
    */

    // ---------------------- ignore signals to shell ---------------------
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    // ---------------------------- defines --------------------------------
    char buffer[SIZE];
    char *tokens[512];
    char *argv[512];
    // value used to check for read error
    ssize_t read_val;
    // value used to check for parsing error
    int ex_ok;

    // input redirect filename
    char *input_redir_name = NULL;
    // output redirect filename
    char *output_redir_name = NULL;
    // output redirection type: 0 is >, 1 is >>
    int out_redir_type = 0;

    job_list = init_job_list();

    while (1) {
        // --------------------------- cleaning ----------------------------
        // clean tokens and argv
        int i = 0;
        while (tokens[i] != NULL) {
            tokens[i] = NULL;
            argv[i] = NULL;
            i++;
        }

        // clean redirect paths
        input_redir_name = NULL;
        output_redir_name = NULL;

        // clean buffer
        memset(buffer, '\0', SIZE);

        // --------------------------- reap -------------------------------
        // reap child processes
        reap();

// --------------------------- read from stdin ---------------------------
// print out prompt
#ifdef PROMPT
        print_stderr("33sh> ");
#endif

        // fill buffer with current input
        read_val = read(STDIN_FILENO, buffer, SIZE);
        // check for EOF
        if (read_val == 0) {
            cleanup_job_list(job_list);
            exit(EXIT_SUCCESS);
        }
        // check for reading error
        else if (read_val == -1) {
            print_stderr("ERROR - read error.\n");
            cleanup_job_list(job_list);
            exit(EXIT_FAILURE);
        }

        // --------------------- parse and execute  ------------------------
        // parse input, splitting into arguments
        ex_ok = parse(buffer, tokens, argv, &input_redir_name,
                      &output_redir_name, &out_redir_type);

        /* if we didn't have a parsing error, continue with execution
         *  otherwise, read next line of input */
        if (ex_ok == 0) {
            // execute command
            execute(argv, input_redir_name, output_redir_name, out_redir_type);
        }
    }

    // this return should never be hit
    cleanup_job_list(job_list);
    return 1;
}
