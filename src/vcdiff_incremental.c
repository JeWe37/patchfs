#include "vcdiff_incremental.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include <fcntl.h> 
#include <sys/stat.h> 
#include <sys/mman.h> 

#include "vcdiff.h"

static int append_block(struct target_stream* target, size_t pos, size_t size, uint8_t* data) {
    // realloc by doubling capacity
    if (target->num_blocks == target->capacity) {
        target->capacity *= 2;
        target->blocks = realloc(target->blocks, target->capacity * sizeof(struct block));
        if (target->blocks == NULL)
            return -ENOMEM;
    }

    struct block* block = &target->blocks[target->num_blocks++];
    block->pos = pos;
    block->size = size;
    // make a copy of data
    if (target->source_flag) {
        block->data = *(uint8_t**)data;
        target->source_flag = 0;
    } else {
        block->data = malloc(size);
        if (block->data == NULL)
            return -ENOMEM;
        memcpy(block->data, data, size);
    }
    return 0;
}

static int is_in_source(struct source_stream* source, uint8_t* data) {
    return data >= source->data && data < source->data + source->len;
}

int free_data(struct target_stream* target, struct source_stream* source) {
    for (size_t i = 0; i < target->num_blocks; i++) {
        uint8_t* data = target->blocks[i].data;
        if (!is_in_source(source, data))
            free(target->blocks[i].data);
    }
    free(target->blocks);
    return munmap(source->data, source->len);
}

static int _target_write (void *dev, uint8_t *data, size_t offset, size_t len) {
	struct target_stream *target = (struct target_stream *) dev;

	if (target->offset != offset)
		/* Gapped write not supported! */
		return -ENOTSUP;

    int rc = append_block(target, offset, len, data);

    target->offset += len;

    return rc;
}

static int _target_read (void *dev, uint8_t *dest, size_t offset, size_t len) {
	(void) dev;
	(void) dest;
	(void) offset;
	(void) len;

	/* data written into the pipe is gone! */
	return -ENOTSUP;
}

static const vcdiff_driver_t target_driver = {
	.read = _target_read,
	.write = _target_write
};

static int _source_read (void *dev, uint8_t *dest, size_t offset, size_t len) {
    struct source_stream *source = (struct source_stream *) dev;

    source->target->source_flag = 1;

    if (offset + len > source->len)
        return -EINVAL;

    uint8_t* cur_data = source->data + offset;

    memcpy(dest, &cur_data, sizeof(uint8_t*));

	return 0;
}

static const vcdiff_driver_t source_driver = {
	.read = _source_read
};

int read_range(struct target_stream* target, size_t offset, size_t len, uint8_t* dest) {
    // binary search for block containing start of range
    size_t left = 0;
    size_t right = target->num_blocks;
    while (left < right) {
        size_t mid = (left + right) / 2;
        if (offset < target->blocks[mid].pos + target->blocks[mid].size)
            right = mid;
        else
            left = mid + 1;
    }
    if (left == target->num_blocks)
        return 0;

    // copy data from first block until end of range
    struct block* block = &target->blocks[left];
    size_t block_offset = offset - block->pos;
    size_t block_len = block->size - block_offset;
    if (block_len > len)
        block_len = len;
    memcpy(dest, block->data + block_offset, block_len);

    // copy data from remaining blocks
    size_t remaining = len - block_len;
    while (remaining > 0) {
        block = &target->blocks[++left];
        if (left >= target->num_blocks)
            return len - remaining;
        block_len = block->size;
        if (block_len > remaining)
            block_len = remaining;
        memcpy(dest + len - remaining, block->data, block_len);
        remaining -= block_len;
    }
    return len;
}

int load_diff(struct target_stream* target, struct source_stream* source, int fd_source, int fd_delta) {
    // mmap source file
    struct stat stat_source;
    if (fstat(fd_source, &stat_source) < 0)
        return -errno;

    // init source stream
    source->len = stat_source.st_size;
    source->data = mmap(NULL, stat_source.st_size, PROT_READ, MAP_PRIVATE, fd_source, 0);
    source->target = target;
    
    // init target stream
    target->source_flag = 0;
    target->offset = 0;
    target->num_blocks = 0;
    target->capacity = 16;
    target->blocks = malloc(target->capacity * sizeof(struct block));
    if (target->blocks == NULL)
        return -ENOMEM;

    vcdiff_t ctx;

    // init vcdiff context
    vcdiff_init(&ctx);
    vcdiff_set_source_driver(&ctx, &source_driver, source);
    vcdiff_set_target_driver(&ctx, &target_driver, target);

    int rc = 0;
	uint8_t delta_buf[16 * 1024];
	size_t delta_len;
	while ((delta_len = read(fd_delta, delta_buf, sizeof(delta_buf))) > 0) {
		rc = vcdiff_apply_delta(&ctx, delta_buf, delta_len);
		if (rc < 0)
			goto exit;
	}

    rc = vcdiff_finish(&ctx);

exit:
	if (rc < 0)
		fprintf(stderr, "Error while applying delta: %s\n", vcdiff_error_str(&ctx));

	return rc;
}
