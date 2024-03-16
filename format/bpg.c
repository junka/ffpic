#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "basemedia.h"
#include "bitstream.h"
#include "byteorder.h"
#include "file.h"
#include "bpg.h"
#include "utils.h"
#include "vlog.h"

VLOG_REGISTER(bpg, DEBUG)

static int
BPG_probe(const char *filename)
{
    struct bpg_file header;
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
      VERR(bpg, "fail to open %s\n", filename);
      return -ENOENT;
    }
    fread(&header, sizeof(header), 1, f);
    if (SWAP(header.file_magic) == 0x425047fb) {
        return 0;
    }

    fclose(f);
    return -EINVAL;
}

uint32_t read_ue7(FILE *f)
{
    uint32_t ret = 0;
    uint8_t value = fgetc(f);
    ret = value & 0x7f;
    while ((value & 0x80) != 0) {
        ret <<= 7;
        value = fgetc(f);
        ret |= (value & 0x7f);
    }
    return ret;
}

struct pic*
BPG_load(const char *filename, int skip_flag UNUSED)
{
    struct pic *p = pic_alloc(sizeof(BPG));
    BPG *h = p->pic;
    FILE *f = fopen(filename, "rb");
    fread(&h->head, sizeof(struct bpg_file), 1, f);
    h->head.file_magic = SWAP(h->head.file_magic);
    h->picture_width = read_ue7(f);
    h->picture_height = read_ue7(f);
    h->picture_data_length = read_ue7(f);
    if (h->head.extension_present_flag) {
        // uint32_t extension_len = read_ue7(f);
        while (1) {
            // uint32_t tag = read_ue7(f);
            // uint32_t tag_len = read_ue7(f);
            // if (tag == 5) {
            //     //TODO
            // } else {
            //     //TODO
            // }
        }
    }

    fclose(f);

    return p;
}

static void
BPG_free(struct pic *p)
{
    pic_free(p);
}

static void
BPG_info(FILE *f, struct pic *p)
{
    BPG *h = (BPG *)p->pic;
    fprintf(f, "BPG file format:\n");
    fprintf(f, "pixel_format %d, alpha1 %d, bit_depth %d\n", h->head.pixel_format, h->head.alpha1_flag, h->head.bit_depth_minus_8 + 8);
    fprintf(f, "color_space %d, extension %d, alpha2 %d, limited_range %d\n", h->head.color_space, h->head.extension_present_flag, h->head.alpha2_flag, h->head.limited_range_flag);
    fprintf(f, "width %u, height %u, datalen %u\n", h->picture_width, h->picture_height,
            h->picture_data_length);
}

static struct file_ops bpg_ops = {
    .name = "BPG",
    .probe = BPG_probe,
    .load = BPG_load,
    .free = BPG_free,
    .info = BPG_info,
};

void BPG_init(void) {
    file_ops_register(&bpg_ops);
}
