#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

#include "decoder.h"
#include "helpers.h"
#include "scanning.h"

int nonce = 0;

// Reference: https://en.wikibooks.org/wiki/X86_Assembly/Control_Flow .
char *x86_64_branching_inst[] = {
    // Unconditional jump.
    "jmp",
    // Jump based on status flags.
    "je", "jne", "jg", "jge", "ja", "jae", "jl", "jle", "jb", "jbe", "jo",
    "jno", "jz", "jnz", "js", "jns",
    // Conditional jump based on {,e,r}cx registers.
    "jcxz", "jecxz", "jrcxz",
    // Loop instructions.
    "loop", "loope", "loopne", "loopnz", "loopz",
    // Function call and return.
    "call", "ret",
    NULL
};

int execute_gas(char *input_fn, char *output_fn) {
    char command[128] = { '\0' };
    ASSERT(snprintf(command, sizeof(command), "as --64 -o %s %s", output_fn, input_fn)
        < sizeof(command));
    return system(command);
}

void mkdir_p(char *dir) {
    char *dir_copy = strdup(dir);
    char *subdir = dirname(dir_copy);
    int is_root = (strlen(subdir) == 1) && ((subdir[0] == '.') || (subdir[0] == '/'));
    if (!is_root)
        mkdir_p(subdir);
    free(dir_copy);
    ASSERT((mkdir(dir, 0777) == 0) || (errno == EEXIST));
}

#ifndef __linux__
    #error "sendfile() between non-socket fds is linux-only"
#endif
void copy_file(char *in_path, char *dir, char *fn) {
    mkdir_p(dir);
    char out_path[PATH_MAX];
    ASSERT(snprintf(out_path, sizeof(out_path), "%s/%s", dir, fn) < sizeof(out_path));
    int out_fd = creat(out_path, 0660);
    ASSERT(out_fd != -1);
    int in_fd = open(in_path, O_RDONLY);
    ASSERT(in_fd != -1);
    off_t off = 0;
    struct stat in_stat = { 0 };
    ASSERT(fstat(in_fd, &in_stat) == 0);
    while ((sendfile(out_fd, in_fd, &off, in_stat.st_size - off) != 1) && (off < in_stat.st_size));
    close(out_fd);
    close(in_fd);
}

int main(int argc, char **argv) {
    // Parse the command line.
    char *output_fn = NULL;
    char *debug_dir = NULL;
    int is_64 = 0;
    int c;
    char *long_option_64 = "64";
    struct option long_options[] = {
        { long_option_64, no_argument, NULL, 0 },
        { 0, 0, 0, 0}
    };
    int long_index;
    while ((c = getopt_long(argc, argv, "d:g:I:o:", long_options, &long_index)) != -1) {
        switch (c) {
        case 0:
            if (long_options[long_index].name == long_option_64) {
                is_64 = 1;
            }
            break;
        case 'g':
            debug_dir = optarg;
            break;
        case 'I':
            // Only here because we pass -I. to gcc and it passes that flag to
            // us too.
            break;
        case 'o':
            output_fn = optarg;
            break;
        default:
            // Just ignore everything else.
            break;
        }
    }

    // Check that we got the flags we were expecting.
    ASSERT(is_64);
    ASSERT(output_fn != NULL);

    // In addition to flags, we take one more option: the path to the assembler
    // file.
    ASSERT(optind == argc - 1);
    char *input_fn = argv[optind];

    // We also need GCC_TRACER_DECODER to be set to the filename to use for the
    // decoder file, otherwise let's just call gas directly.
    char *decoder_fn;
    if ((decoder_fn = getenv("GCC_TRACER_DECODER")) == NULL)
        return execute_gas(input_fn, output_fn);
    decoder_t *decoder = decoder_load(decoder_fn, 1);

    // Set up the temporary file that we will write the instrumented assembly
    // to, (which we'll later use gas to assemble).
    char temp_fn[] = "/tmp/XXXXXX";
    int temp_fd = mkstemp(temp_fn);
    ASSERT(temp_fd != -1);
    FILE *temp_f = fdopen(temp_fd, "w");

    // The following is the state of the parser, which works in a single pass:
    char first_path[PATH_MAX] = { '\0' };
    int do_instrument = 1;
    #define TRACE_CHUNK_MAX_LEN 1000
    id_t trace_chunk[TRACE_CHUNK_MAX_LEN] = { 0 };
    int trace_chunk_len = 0;

    FILE *in_file = fopen(input_fn, "r");
    ASSERT(in_file != NULL);
    char line[1024];
    while (fgets(line, sizeof(line), in_file) != NULL) {
        ASSERT(strlen(line) > 0);
        ASSERT((line[strlen(line) - 1] == '\n') || feof(in_file));

        // Try to parse out a .file directive, they look like this:
        // 	.file	"pretzel.c"
        char *s = line;
        char source_fn[PATH_MAX];
        int found_file_directive =
            skip_exactly("\t.file\t\"", &s) &&
            scan_until_any("\"", &s, source_fn, sizeof(source_fn)) &&
            skip_exactly("\"\n", &s) &&
            *s == '\0';
        if (found_file_directive) {
            // If this is the first file directive we've come across, then it's
            // our best guess for which one C input file this assembly
            // corresponds to.
            strncpy(first_path, source_fn, sizeof(first_path));

            decoder_set_current_file(decoder, source_fn);
            goto done_with_line;
        }

        // If we encounter "# as-tracer-do-not-instrument" then we'll turn off
        // instrumentation. (And the opposite.)
        s = line;
        if (skip_exactly("# as-tracer-do-instrument\n", &s))
            do_instrument = 1;
        else if (skip_exactly("# as-tracer-do-not-instrument\n", &s))
            do_instrument = 0;
        if (!do_instrument) {
            trace_chunk_len = 0;
        }

        // Try to parse out a comment generated by -fverbose-asm, they look
        // like this:
        // # pretzel.c:6:     if (argc != 2) {
        s = line;
        char found_source_fn[PATH_MAX] = { 0 };
        char found_line_no[32] = { 0 };
        char found_line[1024] = { 0 };
        int found_verbose_asm_comment =
            skip_exactly("# ", &s) &&
            scan_until_any(":", &s, found_source_fn, sizeof(found_source_fn)) &&
            skip_exactly(":", &s) &&
            scan_until_any(":", &s, found_line_no, sizeof(found_line_no)) &&
            skip_exactly(":", &s) &&
            scan_until_any("\n", &s, found_line, sizeof(found_line)) &&
            skip_exactly("\n", &s) &&
            *s == '\0';
        if (found_verbose_asm_comment) {
            if (do_instrument) {
                int line_no = atoi(found_line_no);
                id_t line_id = decoder_add_line(decoder, line_no, found_line);

                // If we just saw this line, don't bother adding to the
                // trace_chunk.
                if ((trace_chunk_len == 0) || (trace_chunk[trace_chunk_len - 1] != line_id)) {
                    trace_chunk[trace_chunk_len++] = line_id;
                    ASSERT(trace_chunk_len < TRACE_CHUNK_MAX_LEN);
                }
            }

            goto done_with_line;
        }

        // We need to record and reset the trace_chunk, using the arch-specific
        // asm "record stub", whenever we come across ..
        //   * a "LABEL:" because if execution jumps here, it won't have
        //     executed the previous lines.
        s = line;
        int found_label =
            skip_exactly(".", &s) &&
            scan_until_any(":", &s, NULL, 0) &&
            skip_exactly(":\n\0", &s);

        // (continued)
        //   * any of the arch-specific jmp, call, ret, (etc) instructions that
        //     (might) change the instruction pointer, since this (might be) our
        //     last chance to record what has already been executed.
        s = line;
        int found_branch = 0;
        if (skip_exactly("\t", &s)) {
            for (int i = 0; x86_64_branching_inst[i] != NULL; i++) {
                if (skip_exactly(x86_64_branching_inst[i], &s)) {
                    found_branch = 1;
                    break;
                }
            }
        }

        // (continued)
        if ((found_label || found_branch) && (trace_chunk_len > 0) && do_instrument) {
            // Record the trace chunk.
            id_t chunk_id = decoder_add_chunk(decoder, trace_chunk, trace_chunk_len);

            // Copy the record stub over, with values substituted in.
            ASSERT(fputs("######## RECORD\n", temp_f) > 0);
            FILE *stub_f = fopen(xstr(BASE_DIRECTORY) "/arch/x86_64/record_stub.s", "r");
            ASSERT(stub_f != NULL);
            char stub_line[128];
            char stub_line_part[128];
            while (fgets(stub_line, sizeof(stub_line), stub_f) != NULL) {
                ASSERT(strlen(stub_line) > 0);
                ASSERT((stub_line[strlen(stub_line) - 1] == '\n') || feof(stub_f));

                char *stub_s = stub_line;
                while (scan_until_any("?", &stub_s, stub_line_part, sizeof(stub_line_part))) {
                    ASSERT(fputs(stub_line_part, temp_f) > 0);
                    ASSERT(skip_exactly("?", &stub_s));
                    ASSERT(scan_until_any("?", &stub_s, stub_line_part, sizeof(stub_line_part)));
                    if (strcmp(stub_line_part, "NONCE") == 0) {
                        fprintf(temp_f, "%i", nonce);
                    } else if (strcmp(stub_line_part, "TRACE_CHUNK_ID") == 0) {
                        fprintf(temp_f, "$%i", chunk_id);
                    } else {
                        ASSERT(0);
                    }
                    ASSERT(skip_exactly("?", &stub_s));
                }
                ASSERT(fputs(stub_s, temp_f) > 0);
            }
            nonce++;
            fclose(stub_f);
            ASSERT(fputs("######## END\n", temp_f) > 0);

            trace_chunk_len = 0;
            goto done_with_line;
        }

    done_with_line:
        fputs(line, temp_f);
    }
    fclose(temp_f);

    // If we run past the end of the assembly source, and we have anything left
    // in 'trace_chunk', something has gone really wrong, because any
    // sensible assembly file should end with a 'ret' instruction ..
    ASSERT(trace_chunk_len == 0);

    // Use gas to assemble our instrumented assembly.
    int ret = execute_gas(temp_fn, output_fn);

    // If we're trying to debug, store the original and transformed assembly
    // somewhere.
    if (debug_dir != NULL) {
        char dir[PATH_MAX] = { '\0' };
        char *first_path_copy = strdup(first_path);
        char *first_dir = dirname(first_path_copy);
        ASSERT(snprintf(dir, sizeof(dir), "%s/%s", debug_dir, first_dir) < sizeof(dir));
        free(first_path_copy);
        first_path_copy = strdup(first_path);
        char *first_basename = basename(first_path_copy);
        copy_file(input_fn, dir, first_basename);
        char instd[PATH_MAX] = { '\0' };
        ASSERT(snprintf(instd, sizeof(instd), "%s.inst", first_basename) < sizeof(instd));
        copy_file(temp_fn, dir, instd);
        free(first_path_copy);
        //copy_file_no_clobber(temp_fn, "a/b/c", instrumented_input_fn);
    } else {
        // Otherwise we can throw away the transformed assembly.
        unlink(temp_fn);
    }

    return ret;
}
