#include "shell.h"

// Runs in the CHILD process. args points at the first word of one command;
// the command ends at a "|" token or at the NULL that ends the list.
// Sets up < and > redirection, then replaces the child with the program.
static void run_child(char** args) {
    char* argv[MAXARGS + 1];   // the words left over once < file and > file are taken out
    int argc = 0;

    for (int i = 0; args[i] != NULL && strcmp(args[i], "|") != 0; i++) {
        int is_in = (strcmp(args[i], "<") == 0);
        int is_out = (strcmp(args[i], ">") == 0);

        if (!is_in && !is_out) {
            argv[argc++] = args[i];   // a normal argument
            continue;
        }

        // The word after < or > is the file name
        char* file = args[++i];
        if (file == NULL || strcmp(file, "|") == 0) {
            fprintf(stderr, "syntax error: expected a file name after %s\n", is_in ? "<" : ">");
            exit(1);
        }

        int fd;
        if (is_in) {
            fd = open(file, O_RDONLY);
        } else {
            fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        }
        if (fd < 0) {
            perror(file);
            exit(1);
        }

        // Make fd 0 (stdin) or fd 1 (stdout) refer to the file
        dup2(fd, is_in ? STDIN_FILENO : STDOUT_FILENO);
        close(fd);   // the copy made by dup2 is enough
    }
    argv[argc] = NULL;

    if (argc == 0) {
        exit(0);   // nothing to run, e.g. "> file" on its own
    }

    execvp(argv[0], argv);
    perror("Command not found");   // only reached if execvp fails
    exit(1);
}

// Runs one command, or several joined by pipes: cmd1 | cmd2 | cmd3
// Returns the exit status of the last command (0 = success).
int execute(char** arglist) {
    // Find where each command of the pipeline starts
    int starts[MAXARGS + 1];
    int ncmds = 0;
    starts[ncmds++] = 0;
    for (int i = 0; arglist[i] != NULL; i++) {
        if (strcmp(arglist[i], "|") == 0) {
            starts[ncmds++] = i + 1;
        }
    }

    pid_t pids[MAXARGS + 1];
    int fd[2];            // fd[0] = read end, fd[1] = write end of the newest pipe
    int prev_read = -1;   // read end of the pipe coming from the previous command

    for (int c = 0; c < ncmds; c++) {
        int is_last = (c == ncmds - 1);

        // Every command except the last one sends its output into a new pipe
        if (!is_last && pipe(fd) == -1) {
            perror("pipe failed");
            return 1;
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
            return 1;
        }

        if (pid == 0) {   // ---- child ----
            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);   // read from the previous command
                close(prev_read);
            }
            if (!is_last) {
                close(fd[0]);                    // this child never reads its own pipe
                dup2(fd[1], STDOUT_FILENO);      // write into the pipe
                close(fd[1]);
            }
            run_child(arglist + starts[c]);      // never returns
        }

        // ---- parent ----
        // The parent uses neither end, so it closes them. If it kept the
        // write end open, the reader would wait for end-of-file forever.
        if (prev_read != -1) close(prev_read);
        if (!is_last) {
            close(fd[1]);
            prev_read = fd[0];   // the next command will read from here
        }
        pids[c] = pid;
    }

    // Wait for every child; the status kept is that of the last command
    int status = 0;
    for (int c = 0; c < ncmds; c++) {
        waitpid(pids[c], &status, 0);
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 1;   // killed by a signal
}
