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
static void load(decoder_t *d, id_t *id) {
    ASSERT(d->f != NULL);

    char line[1024];
    while (fgets(line, sizeof(line), d->f) != NULL) {
        ASSERT(strlen(line) > 0);
        ASSERT((line[strlen(line) - 1] == '\n') || feof(d->f));

        id_t new_id = -1;

        // (Current) file directives look like:
        // "$FILE_ID F path"
        char *s = line;
        char file_id_s[32];
        char path[PATH_MAX];
        int found_line =
            scan_until_any(" ", &s, file_id_s, sizeof(file_id_s)) &&
            skip_exactly(" F ", &s) &&
            scan_until_any("\n", &s, path, sizeof(path)) &&
            skip_exactly("\n\0", &s);
        if (found_line) {
            // Whatever information we're caching about the current (now
            // previous) file, dump it.
            if (d->map_current_file_line_no_to_data != NULL)
                int_line_data_t_map_free(&(d->map_current_file_line_no_to_data));

            // Track the new current file.
            strncpy(d->current_file_name, path, sizeof(d->current_file_name));
            new_id = d->current_file_id = atoi(file_id_s);
            goto done_with_line;
        }

        // Line directives look like:
        // "$LINE_ID L $FILE_ID path`line_number line"
        s = line;
        char line_id_s[32];
        line_data_t *line_data = malloc(sizeof(line_data_t));
        char line_no_s[32];
        found_line =
            scan_until_any(" ", &s, line_id_s, sizeof(line_id_s)) &&
            skip_exactly(" L ", &s) &&
            scan_until_any(" ", &s, file_id_s, sizeof(file_id_s)) &&
            skip_exactly(" ", &s) &&
            scan_until_any("`", &s, line_data->path, sizeof(line_data->path)) &&
            skip_exactly("`", &s) &&
            scan_until_any(" ", &s, line_no_s, sizeof(line_no_s)) &&
            skip_exactly(" ", &s) &&
            scan_until_any("\n", &s, line_data->content, sizeof(line_data->content)) &&
            skip_exactly("\n\0", &s);
        if (found_line) {
            // Store away the line_id -> line data mapping.
            ASSERT(sscanf(line_id_s, "%u", &(line_data->line_id)));
            ASSERT(sscanf(file_id_s, "%u", &(line_data->file_id)));
            line_data->line_no = atoi(line_no_s);
            id_t_line_data_t_map_put(&(d->map_line_id_to_data), line_data->line_id, line_data);

            // This file must be the current file.
            ASSERT(d->current_file_id == line_data->file_id);
            int_line_data_t_map_put(&(d->map_current_file_line_no_to_data), line_data->line_no, line_data);
            new_id = line_data->line_id;
            goto done_with_line;
        }
        free(line_data);

        // Chunk directives look like:
        // "$CHUNK_ID C $LINE_ID1 $LINE_ID2.."
        s = line;
        char id_s[32];
        if (scan_until_any(" ", &s, id_s, sizeof(id_s)) && skip_exactly(" C", &s)) {
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
            new_id = chunk_id;
            goto done_with_line;
        }

        fprintf(stderr, "don't know how to handle: %s", line);
        ASSERT(0);

    done_with_line:
        // If we were sent here, new_id must be valid and equal to the next
        // available free id.
        ASSERT(new_id != -1);
        ASSERT(new_id == d->last_free_id++);

        // Can we store this (one) new id?
        if (id != NULL) {
            ASSERT(*id == -1);
            *id = new_id;
        }
    }

    // If we expected one id, we better have gotten it.
    ASSERT((id == NULL) || (*id != 0));
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
    load(d, NULL);

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

static id_t add_one(decoder_t *d, const char *fmt, ...) {
    // Note where we are in the decoder file, we'll rewind to this spot to play
    // back what we're about to write.
    ASSERT(d->f != NULL);
    long off = ftell(d->f);
    ASSERT(off != -1);

    // Write the new decoder data to the decoder file: including the new id.
    char new_fmt[1024];
    ASSERT(snprintf(new_fmt, sizeof(new_fmt), "%i %s", d->last_free_id, fmt) < sizeof(new_fmt));
    va_list args;
    va_start(args, fmt);
    ASSERT(vfprintf(d->f, new_fmt, args) >= 0);
    va_end(args);

    // load() will do the right thing for the new decoder file lines we just
    // added.
    ASSERT(fseek(d->f, off, SEEK_SET) == 0);
    id_t id = -1;
    load(d, &id);
    return id;
}

void decoder_set_current_file(decoder_t *d, char *fn) {
    d->current_file_id = add_one(d, "F %s\n", fn);
}

id_t decoder_add_line(decoder_t *d, int line_no, char *line) {
    // We don't want to add the same line over and over again (for however many
    // times it appears in the assembler).
    line_data_t *data = int_line_data_t_map_lookup(d->map_current_file_line_no_to_data, line_no);
    if (data != NULL) {
        return data->line_id;
    }

    id_t id = add_one(d, "L %i %s`%i %s\n", d->current_file_id, d->current_file_name, line_no, line);
    return id;
}

id_t decoder_add_chunk(decoder_t *d, id_t *chunk, int chunk_len) {
    ASSERT(chunk_len > 0);
    char buffer[10000];
    int off = snprintf(buffer, sizeof(buffer), "C");
#define CHECK_OFF ASSERT(off < sizeof(buffer) - 1);
    CHECK_OFF
    for (int i = 0; (i < chunk_len) && (chunk[i] != 0); i++) {
        off += snprintf(buffer + off, sizeof(buffer) - off, " %i", chunk[i]);
        CHECK_OFF
    }
    snprintf(buffer + off, sizeof(buffer) - off, "\n");
    CHECK_OFF
    return add_one(d, buffer);
}
