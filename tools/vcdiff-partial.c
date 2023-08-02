#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "vcdiff_incremental.h"

#define BUFSIZE 1024 * 1024

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s [dict]\n", argv[0]);
        return 1;
    }
    
    int source_fd = open(argv[1], O_RDONLY);

    struct target_stream target;
    struct source_stream source;

    int rc = load_diff(&target, &source, source_fd, STDIN_FILENO);
    if (rc < 0) {
        fprintf(stderr, "Error loading diff: %s\n", strerror(-rc));
        goto end;
    }
    
    uint8_t buf[BUFSIZE];
    size_t read, offset = 0;
    do {
        read = read_range(&target, offset, BUFSIZE, buf);
        rc = write(STDOUT_FILENO, buf, read);
        if (rc < 0) {
            fprintf(stderr, "Error writing to stdout: %s\n", strerror(-rc));
            goto exit;
        }
        offset += read;
    } while (read == BUFSIZE);

    rc = 0;
exit:
    free_data(&target, &source);
end:
    return rc;
}
