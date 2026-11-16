#include "shell.h"

int main() {
    char* cmdline;
    char** arglist;

    // Tell Readline to ask our function for matches when TAB is pressed
    rl_attempted_completion_function = shell_completion;

    // readline() prints the prompt, lets the user edit the line (arrow keys,
    // TAB...) and returns it in malloc'd memory. It returns NULL on Ctrl+D.
    while ((cmdline = readline(PROMPT)) != NULL) {
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

        if ((arglist = tokenize(cmdline)) != NULL) {
            // Built-ins run inside the shell itself; only fork for external commands
            if (handle_builtin(arglist) == 0) {
                execute(arglist);
            }

            // Free the memory allocated by tokenize()
            for (int i = 0; arglist[i] != NULL; i++) {
                free(arglist[i]);
            }
            free(arglist);
        }
        free(cmdline);
    }

    printf("\nShell exited.\n");
    return 0;
}
