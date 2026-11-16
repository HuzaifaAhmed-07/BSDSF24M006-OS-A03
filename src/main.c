#include "shell.h"

// Runs ONE command (there is no ';' in it) and returns its exit status.
int run_command(char* cmd) {
    char** arglist = tokenize(cmd);
    if (arglist == NULL) {
        return 0;   // blank
    }

    // Is the last token "&"? Then it is a background command.
    int n = 0;
    while (arglist[n] != NULL) n++;

    int background = 0;
    if (strcmp(arglist[n - 1], "&") == 0) {
        background = 1;
        free(arglist[n - 1]);    // the & is not an argument of the program
        arglist[n - 1] = NULL;
    }

    int status = 0;
    if (arglist[0] != NULL) {
        // Built-ins run inside the shell itself; only fork for external commands
        if (handle_builtin(arglist) == 0) {
            status = execute(arglist, background);
        }
    }

    // Free the memory allocated by tokenize()
    for (int i = 0; arglist[i] != NULL; i++) {
        free(arglist[i]);
    }
    free(arglist);
    return status;
}

// Splits a line at every ';' (but not inside quotes) and runs the pieces
// one after another, left to right.
void run_line(char* line) {
    char* start = line;   // start of the piece being collected
    char quote = 0;       // the quote character we are inside, or 0

    for (char* p = line; ; p++) {
        if (quote != 0) {
            if (*p == quote) quote = 0;            // closing quote
        } else if (*p == '"' || *p == '\'') {
            quote = *p;                            // opening quote
        }

        if (*p == '\0' || (*p == ';' && quote == 0)) {
            int at_end = (*p == '\0');
            *p = '\0';             // cut the line here
            run_command(start);    // returns only when this piece is finished
            if (at_end) break;
            start = p + 1;         // the next piece begins after the ';'
        }
    }
}

int main() {
    char* cmdline;

    // Tell Readline to ask our function for matches when TAB is pressed
    rl_attempted_completion_function = shell_completion;

    while (1) {
        // Before every prompt, collect background children that have finished
        reap_jobs();

        // readline() prints the prompt, lets the user edit the line (arrow keys,
        // TAB...) and returns it in malloc'd memory. It returns NULL on Ctrl+D.
        cmdline = readline(PROMPT);
        if (cmdline == NULL) {
            break;
        }

        // Nothing typed: do not store it, just show the prompt again
        if (cmdline[0] == '\0') {
            free(cmdline);
            continue;
        }

        // !n : swap the line for command number n from history.
        // This must happen BEFORE tokenizing and BEFORE saving to history.
        if (cmdline[0] == '!') {
            char* old = get_history_cmd(atoi(cmdline + 1));
            if (old == NULL) {
                printf("%s: no such command in history\n", cmdline);
                free(cmdline);
                continue;
            }
            free(cmdline);
            cmdline = strdup(old);
            printf("%s\n", cmdline);    // show what is about to run
        }

        add_to_history(cmdline);   // our own list: 'history' and !n
        add_history(cmdline);      // Readline's list: Up/Down arrow keys

        run_line(cmdline);
        free(cmdline);
    }

    printf("\nShell exited.\n");
    return 0;
}
