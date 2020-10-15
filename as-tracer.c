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

    FILE *f = fopen(input_fn, "r");
    char line[128];
    while (fgets(line, sizeof(line), f) != NULL) {
        // We should have sized `line` to the longest single line that we can
        // receive, otherwise our parsing is wrong.
        ASSERT(strlen(line) > 0);
        ASSERT((line[strlen(line) - 1] == '\n') || feof(f));
        printf(" --------- %s", line);
    }

    return 0;
}
