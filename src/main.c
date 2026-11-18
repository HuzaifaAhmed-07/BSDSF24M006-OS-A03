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

    // Replace $NAME words by their values before anything is run
    expand_variables(arglist);

    int status = 0;
    if (arglist[0] != NULL) {
        if (handle_assignment(arglist)) {
            // NAME=value : stored inside the shell, nothing to run
        } else if (handle_builtin(arglist) == 0) {
            // Built-ins run inside the shell itself; only fork for external commands
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

// Removes spaces and tabs from both ends of s. The end is cut off in place;
// the return value points at the first character that is not a space.
char* trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t')) end--;
    *end = '\0';
    return s;
}

// Does the line begin with the word "if"?
int is_if(const char* line) {
    return strncmp(line, "if", 2) == 0 && (line[2] == ' ' || line[2] == '\t');
}

// Handles:   if <command>
//            then
//                <commands>
//            else              (the else part is optional)
//                <commands>
//            fi
// if_line is the first line ("if <command>"). The rest is read here.
void run_if_block(char* if_line) {
    char* then_lines[MAX_BLOCK];   // commands between then and else/fi
    char* else_lines[MAX_BLOCK];   // commands between else and fi
    int then_count = 0;
    int else_count = 0;

    int state = 0;      // 0 = waiting for "then", 1 = in then-block, 2 = in else-block
    int finished = 0;   // becomes 1 when "fi" is read
    int ok = 1;         // becomes 0 on a syntax error

    // Special mode: keep reading lines (with a "> " prompt) until "fi"
    while (ok && !finished) {
        char* raw = readline("> ");
        if (raw == NULL) {   // Ctrl+D in the middle of the block
            printf("\nsyntax error: reached end of input, expected 'fi'\n");
            ok = 0;
            break;
        }
        char* line = trim(raw);

        if (state == 0) {
            if (strcmp(line, "then") == 0) {
                state = 1;
            } else if (line[0] != '\0') {
                printf("syntax error: expected 'then' after if\n");
                ok = 0;
            }
        } else if (strcmp(line, "fi") == 0) {
            finished = 1;
        } else if (strcmp(line, "else") == 0 && state == 1) {
            state = 2;
        } else if (line[0] != '\0') {
            // An ordinary command: save it in the block we are in
            if (state == 1 && then_count < MAX_BLOCK) {
                then_lines[then_count++] = strdup(line);
            } else if (state == 2 && else_count < MAX_BLOCK) {
                else_lines[else_count++] = strdup(line);
            }
        }
        free(raw);
    }

    if (ok) {
        // Run the condition: everything after the word "if".
        // Exit status 0 means success, which counts as "true".
        int status = run_command(trim(if_line + 2));

        if (status == 0) {
            for (int i = 0; i < then_count; i++) run_line(then_lines[i]);
        } else {
            for (int i = 0; i < else_count; i++) run_line(else_lines[i]);
        }
    }

    for (int i = 0; i < then_count; i++) free(then_lines[i]);
    for (int i = 0; i < else_count; i++) free(else_lines[i]);
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

        char* line = trim(cmdline);   // ignore spaces at the start and end

        // Nothing typed: do not store it, just show the prompt again
        if (line[0] == '\0') {
            free(cmdline);
            continue;
        }

        // !n : swap the line for command number n from history.
        // This must happen BEFORE tokenizing and BEFORE saving to history.
        if (line[0] == '!') {
            char* old = get_history_cmd(atoi(line + 1));
            if (old == NULL) {
                printf("%s: no such command in history\n", line);
                free(cmdline);
                continue;
            }
            free(cmdline);
            cmdline = strdup(old);
            line = cmdline;
            printf("%s\n", line);    // show what is about to run
        }

        add_to_history(line);   // our own list: 'history' and !n
        add_history(line);      // Readline's list: Up/Down arrow keys

        if (is_if(line)) {
            run_if_block(line);   // reads the rest of the block itself
        } else {
            run_line(line);
        }
        free(cmdline);
    }

    printf("\nShell exited.\n");
    return 0;
}
