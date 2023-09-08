#include <stdint.h>
#include <stdio.h>

#include "file.h"
#include "vlog.h"

int main(int argc, const char *argv[])
{
    int ret;
    if (argc < 2) {
        printf("Please input a valid picture path\n");
        return -1;
    }
    const char *filename = argv[1];
    FILE *logf = fopen("picinfo.log", "w+");
    vlog_init();
    vlog_openlog_stream(logf);

    file_ops_init();
    struct file_ops *ops = file_probe(filename);
    if (ops == NULL) {
        printf("file format is not supported\n");
        goto exit;
    }
    struct pic *p = file_load(ops, filename);
    if (!p) {
        goto exit;
    }

    file_info(ops, p);

    file_free(ops, p);
exit:
    vlog_uninit();
    return 0;
}