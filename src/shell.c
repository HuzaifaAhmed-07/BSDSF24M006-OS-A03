#include "shell.h"

// True for characters that are a token all by themselves
static int is_operator(char c) {
    return c == '<' || c == '>' || c == '|' || c == '&';
}

// Splits a command line into tokens (words).
//   ls -l>out.txt     ->  "ls" "-l" ">" "out.txt"
//   grep "a b" f.txt  ->  "grep" "a b" "f.txt"     (quotes group, then vanish)
// Returns a NULL-terminated array, or NULL if the line has no tokens.
char** tokenize(char* cmdline) {
    if (cmdline == NULL) {
        return NULL;
    }

    char** arglist = (char**) malloc(sizeof(char*) * (MAXARGS + 1));
    char* buf = (char*) malloc(strlen(cmdline) + 1);   // one token can never be longer than the line
    char* cp = cmdline;
    int argnum = 0;

    while (*cp != '\0' && argnum < MAXARGS) {
        while (*cp == ' ' || *cp == '\t') cp++;   // skip whitespace between tokens
        if (*cp == '\0') break;

        // <, >, | and & are tokens on their own, even with no spaces around them
        if (is_operator(*cp)) {
            buf[0] = *cp++;
            buf[1] = '\0';
            arglist[argnum++] = strdup(buf);
            continue;
        }

        // An ordinary word: runs until whitespace or an operator
        int len = 0;
        while (*cp != '\0' && *cp != ' ' && *cp != '\t' && !is_operator(*cp)) {
            if (*cp == '"' || *cp == '\'') {
                // Quoted part: copy everything up to the closing quote,
                // spaces included, but leave out the quote marks themselves
                char quote = *cp++;
                while (*cp != '\0' && *cp != quote) {
                    buf[len++] = *cp++;
                }
                if (*cp == quote) cp++;
            } else {
                buf[len++] = *cp++;
            }
        }
        buf[len] = '\0';
        arglist[argnum++] = strdup(buf);
    }

    free(buf);

    if (argnum == 0) {   // blank line
        free(arglist);
        return NULL;
    }

    arglist[argnum] = NULL;
    return arglist;
}

// Returns 1 if the command was a built-in (and has been run), 0 otherwise
int handle_builtin(char** arglist) {
    if (strcmp(arglist[0], "exit") == 0) {
        printf("Shell exited.\n");
        exit(0);
    }

    if (strcmp(arglist[0], "cd") == 0) {
        char* dir = arglist[1];
        if (dir == NULL) {
            dir = getenv("HOME");      // plain "cd" goes to the home directory
        }
        if (dir == NULL) {
            fprintf(stderr, "cd: HOME not set\n");
        } else if (chdir(dir) != 0) {
            perror("cd");              // e.g. "cd: No such file or directory"
        }
        return 1;
    }

    if (strcmp(arglist[0], "help") == 0) {
        printf("Built-in commands:\n");
        printf("  cd <directory>  Change the current working directory\n");
        printf("  exit            Exit the shell\n");
        printf("  help            Show this help message\n");
        printf("  history         Show recent commands (use !n to re-run number n)\n");
        printf("  jobs            List background jobs\n");
        return 1;
    }

    if (strcmp(arglist[0], "history") == 0) {
        print_history();
        return 1;
    }

    if (strcmp(arglist[0], "jobs") == 0) {
        print_jobs();
        return 1;
    }

    return 0;   // not a built-in
}

/* ---------------- Command history ---------------- */

static char* history[HISTORY_SIZE];   // history[0] is the oldest command
static int history_count = 0;         // how many slots are in use

void add_to_history(const char* cmdline) {
    if (history_count == HISTORY_SIZE) {
        // History is full: throw away the oldest and shift the rest up
        free(history[0]);
        for (int i = 1; i < HISTORY_SIZE; i++) {
            history[i - 1] = history[i];
        }
        history_count--;
    }
    history[history_count++] = strdup(cmdline);
}

// Returns command number n (1-based), or NULL if n is out of range
char* get_history_cmd(int n) {
    if (n < 1 || n > history_count) {
        return NULL;
    }
    return history[n - 1];
}

void print_history(void) {
    for (int i = 0; i < history_count; i++) {
        printf("%4d  %s\n", i + 1, history[i]);
    }
}

/* ---------------- Tab completion (GNU Readline) ---------------- */

static char* builtin_names[] = { "cd", "exit", "help", "history", "jobs", NULL };

// Readline calls this again and again; each call must return ONE match,
// and NULL when there are no more. state == 0 means "new TAB press, start over".
static char* command_generator(const char* text, int state) {
    static int bi;              // where we are in builtin_names
    static char* path_copy;     // our own copy of $PATH (strtok modifies it)
    static char* dir_name;      // the PATH directory being searched now
    static DIR* dp;             // that directory, opened
    static size_t len;          // length of what the user typed so far
    struct dirent* entry;

    if (state == 0) {
        bi = 0;
        len = strlen(text);
        if (dp != NULL) { closedir(dp); dp = NULL; }
        free(path_copy);
        char* path = getenv("PATH");
        path_copy = strdup(path ? path : "");
        dir_name = strtok(path_copy, ":");
        if (dir_name != NULL) dp = opendir(dir_name);
    }

    // 1) built-in commands that start with the typed text
    while (builtin_names[bi] != NULL) {
        char* name = builtin_names[bi++];
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    // 2) programs in each directory listed in $PATH
    while (dir_name != NULL) {
        if (dp != NULL) {
            while ((entry = readdir(dp)) != NULL) {
                if (entry->d_name[0] != '.' && strncmp(entry->d_name, text, len) == 0) {
                    return strdup(entry->d_name);
                }
            }
            closedir(dp);
            dp = NULL;
        }
        dir_name = strtok(NULL, ":");
        if (dir_name != NULL) dp = opendir(dir_name);
    }

    return NULL;   // no more matches
}

// Readline calls this when TAB is pressed.
// start = position in the line where the word being completed begins.
char** shell_completion(const char* text, int start, int end) {
    (void) end;
    if (start == 0) {
        // First word on the line -> it is a command name
        return rl_completion_matches(text, command_generator);
    }
    // Any other word -> return NULL so Readline does its default
    // filename completion
    return NULL;
}

/* ---------------- Background jobs ---------------- */

typedef struct {
    pid_t pid;            // process ID of the background child
    char cmd[MAX_LEN];    // the command text, kept only for display
} job_t;

static job_t jobs[MAX_JOBS];
static int job_count = 0;

// Remember a child that was started with & and is not being waited for
void add_job(pid_t pid, char** arglist) {
    if (job_count == MAX_JOBS) {
        fprintf(stderr, "jobs: job list is full, not tracking %d\n", pid);
        return;
    }

    job_t* job = &jobs[job_count++];
    job->pid = pid;

    // Join the words back into one string for the jobs listing
    job->cmd[0] = '\0';
    for (int i = 0; arglist[i] != NULL; i++) {
        strncat(job->cmd, arglist[i], MAX_LEN - strlen(job->cmd) - 1);
        strncat(job->cmd, " ", MAX_LEN - strlen(job->cmd) - 1);
    }

    printf("[%d] %d\n", job_count, pid);
}

// Collect every background child that has finished (no zombies left behind).
// WNOHANG = "do not wait": waitpid returns 0 at once if nobody has finished.
void reap_jobs(void) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].pid == pid) {
                printf("[Done] %d  %s\n", pid, jobs[i].cmd);
                // Remove entry i by shifting the later entries up
                for (int j = i; j < job_count - 1; j++) {
                    jobs[j] = jobs[j + 1];
                }
                job_count--;
                break;
            }
        }
    }
}

void print_jobs(void) {
    reap_jobs();   // drop anything that has just finished
    if (job_count == 0) {
        printf("No background jobs.\n");
        return;
    }
    for (int i = 0; i < job_count; i++) {
        printf("[%d] %d  Running  %s\n", i + 1, jobs[i].pid, jobs[i].cmd);
    }
}
