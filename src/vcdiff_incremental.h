#ifndef VCDIFF_INCREMENTAL_H
#define VCDIFF_INCREMENTAL_H

#include <stdint.h>
#include <stddef.h>

struct block {
    size_t pos;
    size_t size;
    uint8_t* data;
};

struct target_stream {
    int source_flag;
    size_t offset;
    struct block* blocks;
    size_t num_blocks;
    size_t capacity;
};

struct source_stream {
    size_t len;
    uint8_t* data;
    struct target_stream* target;
};

int free_data(struct target_stream* target, struct source_stream* source);

int read_range(struct target_stream* target, size_t offset, size_t len, uint8_t* dest);

int load_diff(struct target_stream* target, struct source_stream* source, int fd_source, int fd_delta);
#endif
