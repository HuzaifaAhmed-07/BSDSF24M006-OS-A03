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

    if (strcmp(arglist[0], "set") == 0) {
        print_vars();
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

static char* builtin_names[] = { "cd", "exit", "help", "history", "jobs", "set", NULL };

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

/* ---------------- Shell variables ---------------- */

// One variable = one node of a singly linked list
typedef struct var {
    char* name;
    char* value;
    struct var* next;
} var_t;

static var_t* var_list = NULL;   // head of the list (empty at start)

// Create the variable, or change its value if it already exists
void set_var(const char* name, const char* value) {
    for (var_t* v = var_list; v != NULL; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            free(v->value);              // throw away the old value
            v->value = strdup(value);
            return;
        }
    }

    // Not found: make a new node and put it at the front of the list
    var_t* v = (var_t*) malloc(sizeof(var_t));
    v->name = strdup(name);
    v->value = strdup(value);
    v->next = var_list;
    var_list = v;
}

// Returns the value of a variable, or NULL if there is no such variable
char* get_var(const char* name) {
    for (var_t* v = var_list; v != NULL; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            return v->value;
        }
    }
    return NULL;
}

// The "set" built-in: print every variable as NAME=value
void print_vars(void) {
    for (var_t* v = var_list; v != NULL; v = v->next) {
        printf("%s=%s\n", v->name, v->value);
    }
}

// If the command is a single word of the form NAME=value, store the
// variable and return 1. Otherwise return 0 and do nothing.
int handle_assignment(char** arglist) {
    if (arglist[1] != NULL) {
        return 0;   // more than one word, so it is an ordinary command
    }

    char* eq = strchr(arglist[0], '=');   // points at the '=' (or NULL)
    if (eq == NULL || eq == arglist[0]) {
        return 0;   // no '=' at all, or nothing before it
    }

    // The name may use letters, digits and _ and must not start with a digit
    if (isdigit((unsigned char) arglist[0][0])) {
        return 0;
    }
    for (char* p = arglist[0]; p < eq; p++) {
        if (!isalnum((unsigned char) *p) && *p != '_') {
            return 0;
        }
    }

    *eq = '\0';                      // cut "NAME=value" into "NAME" and "value"
    set_var(arglist[0], eq + 1);
    *eq = '=';                       // put the word back as it was
    return 1;
}

// Replace every argument that starts with $ by the value of that variable.
// Runs after tokenizing and before executing.
void expand_variables(char** arglist) {
    for (int i = 0; arglist[i] != NULL; i++) {
        if (arglist[i][0] != '$' || arglist[i][1] == '\0') {
            continue;   // not a variable (a lone "$" is left alone)
        }

        char* name = arglist[i] + 1;      // skip the '$'
        char* value = get_var(name);
        if (value == NULL) {
            value = getenv(name);         // fall back to the environment: $HOME, $PATH ...
        }
        if (value == NULL) {
            value = "";                   // unknown variable becomes empty text
        }

        // The old string may be too small for the value, so allocate a
        // new one of the right size and free the old one
        char* expanded = strdup(value);
        free(arglist[i]);
        arglist[i] = expanded;
    }
}
