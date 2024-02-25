#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>

#include "file.h"
#include "vlog.h"


void usage(void)
{
    printf("\tUsage:\n");
    printf("\t transcode [options] original_file\n");
    printf("\t options = help | codec <jpg>\n");
}

int main(int argc, char *argv[])
{
    int ret;
    int ch;
    char * codec = NULL;
    struct option options[] = {
        {"help", no_argument, NULL, 'h'},
        {"codec", required_argument, NULL, 'c'},
        {0,0,0,0}
    };

    int option_index = 0;
    while ((ch = getopt_long(argc, argv, "hc:", options, &option_index)) !=-1) {
        switch (ch) {
        case 'h':
            usage();
            return 0;
        case 'c':
            printf("codec %s\n", optarg);
            codec = optarg;
            break;
        default:
            break;
        }
    }
    if (optind >= argc) {
        printf("No valid file input\n");
        return -1;
    }
    if (!codec) {
        printf("No valid codec type input\n");
        return -1;
    }

    const char *filename = argv[optind];
    if (0 != access(filename, F_OK|R_OK)) {
        printf("File not exist or can not read\n");
        return -1;
    }
    char newfile[128];

    FILE *logf = fopen("transcode.log", "w+");
    vlog_init();
    vlog_openlog_stream(logf);
    file_ops_init();

    struct file_ops *ops = file_probe(filename);
    if (ops == NULL) {
        printf("file format is not support\n");
        return -1;
    }

    int left = 0, top = 0;
    struct pic *p = file_load(ops, filename, 0);
    printf("width %d\n", p->width);

    struct file_ops *tops = file_find_codec(codec);
    if (tops) {
        printf("find codec for %s\n", codec);
    }
    snprintf(newfile, 128, "%s_transcode.%s", filename, tops->name);
    tops->encode(p, newfile);
    printf("trancode to file %s\n", newfile);

    file_free(ops, p);
    vlog_uninit();
    return 0;
}
