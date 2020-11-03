#include <errno.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "helpers.h"

char *instrumentation_args[] = {
    // The linking process will need these for tracer_support.c.
    "-lrt",
    "-pthread",
    // Tell gcc where to find our include files.
    "-I" xstr(BASE_DIRECTORY),
    // Tell gcc where to find the overridden `as` binary.
    "-B" xstr(BASE_DIRECTORY) "/gcc-bin",
    // We need GCC to annotate asm with the C lines that the instructions
    // correspond to.
    "-fverbose-asm",
    NULL
};

char **concat_args(int c, ...) {
    // Smash all the char*[] together.
    char **args = NULL;
    int args_count = 0;
    va_list ap;
    va_start(ap, c);
    for (int i = 0; i < c; i++) {
        for (char **more_args = va_arg(ap, char **); *more_args != NULL; more_args++) {
            args = realloc(args, sizeof(char *) * ++args_count);
            args[args_count - 1] = *more_args;
        }
    }
    va_end(ap);

    // Add the terminating NULL.
    args = realloc(args, sizeof(char *) * ++args_count);
    args[args_count - 1] = NULL;

    return args;
};

int is_tracer_support(char *s) {
    char *copy = strdup(s);
    ASSERT(copy != NULL);
    int ret = strcmp(basename(copy), "tracer_support.c") == 0;
    free(copy);
    return ret;
}

int main(int argc, char **argv) {
    char *gcc[] = { "gcc", NULL };
    char **args = concat_args(3, gcc, &(argv[1]), instrumentation_args);
    execvp("gcc", args);
    printf("execvp() failed: %s\n", strerror(errno));

    // Control should never get here.
    ASSERT(0);
    return -1;
}
