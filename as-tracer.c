#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(cond) \
    { \
        if (!(cond)) { \
            printf("ASSERT(%s) failed at %s L%i\n", #cond, __FILE__, __LINE__); \
            exit(1); \
        } \
    }

#define NAME "as-tracer"

int skip_one(char c, char **s) {
    if (**s == c) {
        *s += 1;
        return 1;
    }
    return 0;
}

int skip_many(char c, char **s) {
    int ret = 0;
    for (; **s == c; *s += 1) ret = 1;
    return ret;
}

int skip_exactly(char *to_skip, char **s) {
    if (strncmp(*s, to_skip, strlen(to_skip)) == 0) {
        *s += strlen(to_skip);
        return 1;
    }
    return 0;
}

int scan_until(char c, char **s, char *scanned, int scanned_sz) {
    int ret = 0;
    int scanned_off = 0;
    for (; (**s != c) && (**s != '\0'); *s += 1) {
        ASSERT(scanned_off < scanned_sz);
        scanned[scanned_off++] = **s;
        ret = 1;
    }
    return ret;
}

int main(int argc, char **argv) {
    // Parse the command line.
    char *output_fn = NULL;
    int is_64 = 0;
    int c;
    char *long_option_64 = "64";
    struct option long_options[] = {
        { long_option_64, no_argument, NULL, 0 },
        { 0, 0, 0, 0}
    };
    int long_index;
    while ((c = getopt_long(argc, argv, "o:", long_options, &long_index)) != -1) {
        switch (c) {
        case 0:
            if (long_options[long_index].name == long_option_64) {
                is_64 = 1;
            }
            break;
        case 'o':
            output_fn = optarg;
            break;
        default:
            ASSERT(0);
        }
    }

    // Check that we got the flags we were expecting.
    ASSERT(is_64);
    ASSERT(output_fn != NULL);

    // In addition to flags, we take one more option: the path to the assembler
    // file.
    ASSERT(optind == argc - 1);
    char *input_fn = argv[optind];

    // The following is the state of the parser, which works in a single pass:
    // On the first line, we are expecting a .file directive.
    int expecting_file_directive = 1;

    FILE *f = fopen(input_fn, "r");
    char line[128];
    while (fgets(line, sizeof(line), f) != NULL) {
        // We should have sized `line` to the longest single line that we can
        // receive, otherwise our parsing is wrong.
        ASSERT(strlen(line) > 0);
        ASSERT((line[strlen(line) - 1] == '\n') || feof(f));
        printf(" --------- %s", line);

        // Have we found a .file directive?
        char *s = line;
        char source_fn[128] = { 0 };
        int found_file_directive =
            skip_one('\t', &s) &&
            skip_exactly(".file", &s) &&
            skip_one('\t', &s) &&
            skip_one('"', &s) &&
            scan_until('"', &s, source_fn, sizeof(source_fn));
        if (found_file_directive) {
            printf(NAME ": instrumenting '%s'\n", source_fn);
            expecting_file_directive = 0;
            continue;
        } else {
            ASSERT(!expecting_file_directive);
        }
    }

    return 0;
}
