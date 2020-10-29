#include <stdarg.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include "assert.h"
#include "decoder.h"
#include "scanning.h"

MAP_DECL(int, line_data_t);
MAP_DECL(id_t, line_data_t);
MAP_DECL(id_t, chunk_data_t);

// Read d->f until the end, adding to in-memory structures as we go.
static void load(decoder_t *d) {
    ASSERT(d->f != NULL);

    int n_lines = 0;
    char line[1024];
    while (fgets(line, sizeof(line), d->f) != NULL) {
        ASSERT(strlen(line) > 0);
        ASSERT((line[strlen(line) - 1] == '\n') || feof(d->f));

        // Line directives look like:
        // "L $LINE_ID $FILE_ID path`line_number line"
        char *s = line;
        char line_id_s[32];
        char file_id_s[32];
        line_data_t *line_data = malloc(sizeof(line_data_t));
        char line_no_s[32];
        int found_line =
            skip_exactly("L ", &s) &&
            scan_until_any(" ", &s, line_id_s, sizeof(line_id_s)) &&
            skip_exactly(" ", &s) &&
            scan_until_any(" ", &s, file_id_s, sizeof(file_id_s)) &&
            skip_exactly(" ", &s) &&
            scan_until_any("`", &s, line_data->path, sizeof(line_data->path)) &&
            skip_exactly("`", &s) &&
            scan_until_any(" ", &s, line_no_s, sizeof(line_no_s)) &&
            skip_exactly(" ", &s) &&
            scan_until_any("\n", &s, line_data->content, sizeof(line_data->content)) &&
            skip_exactly("\n\0", &s);
        if (found_line) {
            ASSERT(sscanf(line_id_s, "%u", &(line_data->line_id)));
            ASSERT(sscanf(file_id_s, "%u", &(line_data->file_id)));
            line_data->line_no = atoi(line_no_s);
            ASSERT(id_t_line_data_t_map_lookup(d->map_line_id_to_data, line_data->line_id) == NULL);
            id_t_line_data_t_map_put(&(d->map_line_id_to_data), line_data->line_id, line_data);
            n_lines++;
            continue;
        }
        free(line_data);

        // Chunk directives look like:
        // "C $CHUNK_ID $LINE_ID1 $LINE_ID2.."
        s = line;
        char id_s[32];
        if (skip_exactly("C ", &s) && scan_until_any(" ", &s, id_s, sizeof(id_s))) {
            id_t chunk_id = atoi(id_s);
            chunk_data_t *chunk_data = malloc(sizeof(chunk_data_t));
            chunk_data->line_id_count = 0;
            while (1) {
                if (skip_exactly("\n\0", &s)) {
                    break;
                } else if (skip_exactly(" ", &s) && scan_until_any(" \n", &s, id_s, sizeof(id_s))) {
                    id_t line_id = atoi(id_s);
                    chunk_data->line_ids[chunk_data->line_id_count++] = line_id;
                } else ASSERT(0);
            }
            id_t_chunk_data_t_map_put(&(d->map_chunk_id_to_data), chunk_id, chunk_data);
            continue;
        }

        fprintf(stderr, "don't know how to handle: %s", line);
        ASSERT(0);
    }
}

decoder_t *decoder_load(char *fn, int writeable) {
    // Initialize decoder_t.
    decoder_t *d = malloc(sizeof(decoder_t));
    ASSERT((d->f = fopen(fn, "r+")) != NULL);
    d->last_free_id = 1;
    d->current_file_id = -1;
    d->current_file_name[0] = '\0';
    d->map_current_file_line_no_to_data = NULL;
    d->map_line_id_to_data = NULL;
    d->map_chunk_id_to_data = NULL;
    load(d);

    if (writeable) {
        // If we need to write to the decoder file, then we've got to lock it
        // to make sure we're the only one writing.
        ASSERT(flock(fileno(d->f), LOCK_EX) == 0);
        // (It'll get unlocked when the process exits .. not great but works ok
        // for now.)
    } else {
        // Otherwise, let's make sure later write operations fail!
        ASSERT(fclose(d->f) == 0);
        d->f = NULL;
    }

    return d;
}

line_data_t *decoder_lookup_line(decoder_t *d, id_t line_id) {
    return id_t_line_data_t_map_lookup(d->map_line_id_to_data, line_id);
}

chunk_data_t *decoder_lookup_chunk(decoder_t *d, id_t chunk_id) {
    return id_t_chunk_data_t_map_lookup(d->map_chunk_id_to_data, chunk_id);
}

static void add(decoder_t *d, const char *fmt, ...) {
    ASSERT(d->f != NULL);
    long off = ftell(d->f);
    ASSERT(off != -1);
    va_list args;
    va_start(args, fmt);
    ASSERT(vfprintf(d->f, fmt, args) >= 0);
    va_end(args);
    ASSERT(fseek(d->f, off, SEEK_SET) == 0);
    load(d);
}

void decoder_set_current_file(decoder_t *d, char *fn) {
    d->current_file_id = d->last_free_id++;
    strcpy(d->current_file_name, fn);
    if (d->map_current_file_line_no_to_data != NULL)
        int_line_data_t_map_free(&(d->map_current_file_line_no_to_data));
}

id_t decoder_add_line(decoder_t *d, int line_no, char *line) {
    line_data_t *data = int_line_data_t_map_lookup(d->map_current_file_line_no_to_data, line_no);
    if (data != NULL) {
        // We've already added this line (for the current file).
        return data->line_id;
    }
    ASSERT(d->current_file_id != 0);
    id_t id = d->last_free_id++;
    add(d, "L %i %i %s`%i %s\n", id, d->current_file_id, d->current_file_name, line_no, line);
    data = id_t_line_data_t_map_lookup(d->map_line_id_to_data, id);
    int_line_data_t_map_put(&(d->map_current_file_line_no_to_data), line_no, data);
    return id;
}

id_t decoder_add_chunk(decoder_t *d, id_t *chunk, int chunk_len) {
    ASSERT(chunk_len > 0);
    char buffer[10000];
    id_t id = d->last_free_id++;
    int off = snprintf(buffer, sizeof(buffer), "C %i", id);
    ASSERT(off < sizeof(buffer) - 1);
    for (int i = 0; (i < chunk_len) && (chunk[i] != 0); i++) {
        off += snprintf(buffer + off, sizeof(buffer) - off, " %i", chunk[i]);
        ASSERT(off < sizeof(buffer) - 1);
    }
    snprintf(buffer + off, sizeof(buffer) - off, "\n");
    add(d, buffer);
    ASSERT(off < sizeof(buffer) - 1);
    return id;
}
