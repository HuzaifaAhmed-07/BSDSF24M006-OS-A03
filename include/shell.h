#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_LEN 512
#define MAXARGS 64
#define PROMPT "FCIT> "
#define HISTORY_SIZE 20
#define MAX_JOBS 32

// Function prototypes
char** tokenize(char* cmdline);
int execute(char** arglist, int background);
int handle_builtin(char** arglist);
void add_to_history(const char* cmdline);
char* get_history_cmd(int n);
void print_history(void);
char** shell_completion(const char* text, int start, int end);
void add_job(pid_t pid, char** arglist);
void reap_jobs(void);
void print_jobs(void);

#endif // SHELL_H
