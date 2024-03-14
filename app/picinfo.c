#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <getopt.h>

#include "file.h"
#include "vlog.h"
#include "accl.h"

void usage(void)
{
    printf("\tUsage:\n");
    printf("\t picinfo [options h|s] image_file\n");
    printf("\t options = help | skip_decode\n");
}

int main(int argc, char *argv[])
{
    int ret;
    int ch;
    int skip_flag = 0;
    struct option options[] = {{"help", no_argument, NULL, 'h'},
                               {"skip_decode", no_argument, &skip_flag, 1},
                               {0, 0, 0, 0}};
    int option_index = 0;
    while ((ch = getopt_long(argc, argv, "hs", options, &option_index)) !=-1) {
        switch (ch) {
            case 'h':
                usage();
                return 0;
            case 's':
                skip_flag = 1;
                break;
            default:
                usage();
                return 0;
        }
    }
    if (optind >= argc) {
        printf("No valid file input\n");
        return -1;
    }

    const char *filename = argv[optind];
    if (0 != access(filename, F_OK | R_OK)) {
        printf("File not exist or can not read\n");
        return -1;
    }
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
    struct pic *p = file_load(ops, filename, skip_flag);
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
