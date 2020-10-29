#ifndef __DECODER_H__
#define __DECODER_H__

#include <linux/limits.h>
#include <stdint.h>
#include <stdio.h>

#define MAP_DEFN(KEY_T, VAL_T) \
    typedef struct KEY_T##_##VAL_T##_map { \
        KEY_T key; \
        VAL_T *value; \
        struct KEY_T##_##VAL_T##_map *next; \
    } KEY_T##_##VAL_T##_map_t; \
    \
    VAL_T *KEY_T##_##VAL_T##_map_lookup(KEY_T##_##VAL_T##_map_t *, KEY_T); \
    void KEY_T##_##VAL_T##_map_put(KEY_T##_##VAL_T##_map_t **, KEY_T, VAL_T *); \
    void KEY_T##_##VAL_T##_map_free(KEY_T##_##VAL_T##_map_t **);

#define MAP_DECL(KEY_T, VAL_T) \
    VAL_T *KEY_T##_##VAL_T##_map_lookup(KEY_T##_##VAL_T##_map_t *map, KEY_T k) { \
        for (; map != NULL; map = map->next) \
            if (map->key == k) \
                return map->value; \
        return NULL; \
    } \
    \
    void KEY_T##_##VAL_T##_map_put(KEY_T##_##VAL_T##_map_t **map, KEY_T k, VAL_T *p) { \
        KEY_T##_##VAL_T##_map_t *new_map = malloc(sizeof(KEY_T##_##VAL_T##_map_t)); \
        new_map->key = k; \
        new_map->value = p; \
        new_map->next = *map; \
        *map = new_map; \
    } \
    \
    void KEY_T##_##VAL_T##_map_free(KEY_T##_##VAL_T##_map_t **map) { \
        for (KEY_T##_##VAL_T##_map_t *p = *map; p != NULL; ) { \
            KEY_T##_##VAL_T##_map_t *next = p->next; \
            free(p); \
            p = next; \
        } \
        *map = NULL; \
    }

typedef uint32_t id_t;

typedef struct {
    id_t line_id;
    id_t file_id;
    char path[PATH_MAX];
    int line_no;
    char content[1024];
} line_data_t;

typedef struct {
    int line_id_count;
    id_t line_ids[1000];
} chunk_data_t;

MAP_DEFN(int, line_data_t);
MAP_DEFN(id_t, line_data_t);
MAP_DEFN(id_t, chunk_data_t);

typedef struct {
    FILE *f;
    id_t last_free_id;
    id_t current_file_id;
    char current_file_name[PATH_MAX];
    int_line_data_t_map_t *map_current_file_line_no_to_data;
    id_t_line_data_t_map_t *map_line_id_to_data;
    id_t_chunk_data_t_map_t *map_chunk_id_to_data;
} decoder_t;

decoder_t *decoder_load(char *s, int);
line_data_t *decoder_lookup_line(decoder_t *, id_t);
chunk_data_t *decoder_lookup_chunk(decoder_t *, id_t);

// The following are only valid for decoders initialized with writeable=1:
void decoder_set_current_file(decoder_t *, char *);
id_t decoder_add_line(decoder_t *, int, char *);
id_t decoder_add_chunk(decoder_t *, id_t *, int);

#endif
