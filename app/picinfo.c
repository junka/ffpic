#include <stdint.h>
#include <stdio.h>

#include "file.h"
#include "vlog.h"
#include "accl.h"

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

    accl_ops_init();
    file_ops_init();
    struct file_ops *ops = file_probe(filename);
    if (ops == NULL) {
        printf("file format is not supported\n");
        goto exit;
    }
    struct pic *p = file_load(ops, filename);
    if (!p) {
        //for GIF, the info will be stored in the last frame
        p = file_dequeue_pic();
        while (p && !p->pic) {
            file_free(ops, p);
            p = file_dequeue_pic();
        }
        if (!p || !p->pic) {
            goto exit;
        }
    }

    file_info(ops, p);

    file_free(ops, p);
exit:
    vlog_uninit();
    return 0;
}