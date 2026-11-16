#include "shell.h"

int main() {
    char* cmdline;
    char** arglist;

    while ((cmdline = read_cmd(PROMPT, stdin)) != NULL) {
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

        add_to_history(cmdline);

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
