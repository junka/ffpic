#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "vlog.h"
#include "file.h"
#include "avif.h"

VLOG_REGISTER(avif, DEBUG);

static int
AVIF_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct ftyp_box h;
    int len = fread(&h, sizeof(h), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (h.major_brand == TYPE2UINT("ftyp")) {
        if(h.minor_version == TYPE2UINT("avif"))
        return 0;
    }
    return -EINVAL;
}



static int
read_av1c_box(FILE *f, struct box **bn)
{
    struct av1C_box *b = malloc(sizeof(struct av1C_box));
    if (*bn == NULL) {
        *bn = (struct box *)b;
    }
    fread(b, 8, 1, f);
    b->size = SWAP(b->size);

    fread(&b->type + 4, 4, 1, f);

    
    VDBG(avif, "size %d",
                b->size);

    // b->configOBUs = malloc(b->num_of_arrays * sizeof(struct nal_arr));

    VDBG(avif, "av1C: version %d, marker 0x%x", b->version,
                b->marker);
    fseek(f, b->size - 12, SEEK_CUR);
    return b->size;
}

static void
read_meta_box(FILE *f, struct av1_meta_box *meta)
{
    struct box b;
    uint32_t type = read_full_box(f, meta, -1);
    if (type != TYPE2UINT("meta")) {
        VERR(avif, "error, it is not a meta after ftyp");
        return;
    }
    int size = meta->size -= 12;
    while (size) {
        type = read_box(f, &b, size);
        fseek(f, -8, SEEK_CUR);
        VDBG(avif, "type (%s), size %d", UINT2TYPE(type), b.size);
        switch (type) {
        case TYPE2UINT("hdlr"):
            read_hdlr_box(f, &meta->hdlr);
            break;
        case TYPE2UINT("pitm"):
            read_pitm_box(f, &meta->pitm);
            break;
        case TYPE2UINT("iloc"):
            read_iloc_box(f, &meta->iloc);
            break;
        case TYPE2UINT("iinf"):
            read_iinf_box(f, &meta->iinf);
            break;
        case TYPE2UINT("iref"):
            read_iref_box(f, &meta->iref);
            break;
        case TYPE2UINT("iprp"):
            read_iprp_box(f, &meta->iprp, &read_av1c_box);
            break;
        default:
            break;
        }
        size -= b.size;
    }
    
}

static struct pic* 
AVIF_load(const char *filename) {
        struct pic *p = pic_alloc(sizeof(AVIF));
    AVIF *h = p->pic;
    FILE *f = fopen(filename, "rb");
    fseek(f, 0, SEEK_END);
    uint32_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    size -= read_ftyp(f, &h->ftyp);
    h->mdat = malloc(sizeof(struct mdat_box));
    struct box b;
    while (size) {
        uint32_t type = read_box(f, &b, size);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
        case TYPE2UINT("meta"):
            read_meta_box(f, &h->meta);
            break;
        case TYPE2UINT("mdat"):
            h->mdat_num ++;
            h->mdat = realloc(h->mdat, h->mdat_num * sizeof(struct mdat_box));
            read_mdat_box(f, h->mdat + h->mdat_num - 1);
            break;
        default:
            break;
        }
        size -= b.size;
        VDBG(avif, "%s, read %d, left %d", UINT2TYPE(type), b.size, size);
    }

    // extract some info from meta box
    for (int i = 0; i < 2; i ++) {
        if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("ispe")) {
            p->width = ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_width;
            p->height = ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_height;
        }
    }
    // h->items = malloc(h->meta.iloc.item_count * sizeof(struct heif_item));
    // for (int i = 0; i < h->meta.iloc.item_count; i ++) {
    //     h->items[i].item = &h->meta.iloc.items[i];
    // }


    fclose(f);

    return p;
}

static void 
AVIF_free(struct pic *p)
{
    AVIF * a = (AVIF *)p->pic;
    if (a->mdat) {
        free(a->mdat);
    }
    pic_free(p);
}


static void
AVIF_info(FILE *f, struct pic* p)
{
    AVIF * h = (AVIF *)p->pic;
    fprintf(f, "AVIF file format:\n");
    fprintf(f, "-----------------------\n");
    char *s1 = UINT2TYPE(h->ftyp.minor_version);
    fprintf(f, "\tbrand: %s", s1);
    char *s2 = UINT2TYPE(h->ftyp.compatible_brands[1]);
    fprintf(f," compatible %s\n", s2);
    fprintf(f, "\theight: %d, width: %d\n", p->height, p->width);

    fprintf(f, "\tmeta box --------------\n");
    fprintf(f, "\t");
    print_box(f, &h->meta.hdlr);
    s1 = UINT2TYPE(h->meta.hdlr.handler_type);
    fprintf(f, " pre_define=%d,handle_type=\"%s\"", h->meta.hdlr.pre_defined, s1);
    if (h->meta.hdlr.name) {
        fprintf(f, ",name=%s", h->meta.hdlr.name);
    }
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.pitm);
    fprintf(f, " item_id=%d", h->meta.pitm.item_id);
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iloc);
    fprintf(f, "\n");
    for (int i = 0; i < h->meta.iloc.item_count; i ++) {
        fprintf(f, "\t\t");
        if (h->meta.iloc.version == 1) {
            fprintf(f, "construct_method=%d,", h->meta.iloc.items[i].construct_method);
        }
        fprintf(f, "item_id=%d,data_ref_id=%d,base_offset=%lld,extent_count=%d\n", 
            h->meta.iloc.items[i].item_id, h->meta.iloc.items[i].data_ref_id,
            h->meta.iloc.items[i].base_offset, h->meta.iloc.items[i].extent_count);
        for (int j = 0; j < h->meta.iloc.items[i].extent_count; j ++) {
            fprintf(f, "\t\t\t");
            if (h->meta.iloc.version == 1) {
                fprintf(f, "extent_id=%lld,", h->meta.iloc.items[i].extents[j].extent_index);
            }
            fprintf(f, "extent_offset=%lld,extent_length=%lld\n", h->meta.iloc.items[i].extents[j].extent_offset,
                h->meta.iloc.items[i].extents[j].extent_length);
        }
    }
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iinf);
    fprintf(f, "\n");
    for (int i = 0; i < h->meta.iinf.entry_count; i ++) {
        fprintf(f, "\t\t");
        print_box(f, &h->meta.iinf.item_infos[i]);
        s1 = UINT2TYPE(h->meta.iinf.item_infos[i].item_type);
        fprintf(f, " item_id=%d,item_protection_index=%d,item_type=%s",
            h->meta.iinf.item_infos[i].item_id, h->meta.iinf.item_infos[i].item_protection_index,
            s1);
        fprintf(f, "\n");
    }

    fprintf(f, "\t");
    print_box(f, &h->meta.iprp);
    fprintf(f, "\n\t");
    fprintf(f, "\t");
    print_box(f, &h->meta.iprp.ipco);
    fprintf(f, "\n\t");
    for (int i = 0; i < h->meta.iprp.ipco.n_prop; i ++) {
        fprintf(f, "\t\t");
        print_box(f, h->meta.iprp.ipco.property[i]);
        if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("ispe")) {
            fprintf(f, ", width %d, height %d", 
                ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_width,
                ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_height);
        } else if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("av1C")) {
            struct av1C_box * av1c = (struct av1C_box *)(h->meta.iprp.ipco.property[i]);

            fprintf(f, " config marker %d, version %d, seq_profile %d, seq_level_idx_0 %d\n", av1c->marker,
                    av1c->version, av1c->seq_profile, av1c->seq_level_idx_0);
            fprintf(f, "\t\t\t\tseq_tier_0 %d, high_bitdepth %d, twelve_bit %d, monochrome %d\n",
                av1c->seq_tier_0, av1c->high_bitdepth, av1c->twelve_bit, av1c->monochrome);
            fprintf(f, "\t\t\t\tchroma_subsampling_x %d, chroma_subsampling_y %d, chroma_sample_position %d\n",
                av1c->chroma_subsampling_x, av1c->chroma_subsampling_y, av1c->chroma_sample_position);
    
            fprintf(f, "\t\t\t\tinitial_presentation_delay_present %d", av1c->initial_presentation_delay_present);
            if (av1c->initial_presentation_delay_present)
                fprintf(f, " initial_presentation_delay_minus_one %d\n", av1c->initial_presentation_delay_minus_one);            
        } else if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("pixi")) {
            struct pixi_box * pixi = (struct pixi_box *)(h->meta.iprp.ipco.property[i]);
            fprintf(f, "\tchannels : %d\n", pixi->channels);
            for (int i = 0; i < pixi->channels; i ++) {
                fprintf(f, "\t\t\t\t bits_per_channel %d\n", pixi->bits_per_channel[i]);
            }
        }
        fprintf(f, "\n\t");
    }
    fprintf(f, "\t");
    print_box(f, &h->meta.iprp.ipma);
    fprintf(f, "\n");
    for (int i = 0; i < h->meta.iprp.ipma.entry_count; i++) {
        struct ipma_item *ipma = &h->meta.iprp.ipma.entries[i];
        fprintf(f, "\t\titem %d: association_count %d\n", ipma->item_id, ipma->association_count);
        for (int j = 0; j < ipma->association_count; j ++) {
            fprintf(f, "\t\t\tessential %d, id %d \n", ipma->association[j] >> 15, ipma->association[j] & 0x7fff);
        }
    }
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iref);
    fprintf(f, "\n");
    for (int i = 0; i < h->meta.iref.refs_count; i ++) {
        fprintf(f, "\t\t");
        s1 = UINT2TYPE(h->meta.iref.refs[i].type);
        fprintf(f, "ref_type=%s,from_item_id=%d,ref_count=%d", s1,
            h->meta.iref.refs[i].from_item_id,
            h->meta.iref.refs[i].ref_count);
        for (int j = 0; j < h->meta.iref.refs[i].ref_count; j ++) {
            fprintf(f, ",to_item=%d",  h->meta.iref.refs[i].to_item_ids[j]);
        }
        fprintf(f, "\n");
    }
    
}



static struct file_ops avif_ops = {
    .name = "AVIF",
    .probe = AVIF_probe,
    .load = AVIF_load,
    .free = AVIF_free,
    .info = AVIF_info,
};

void AVIF_init(void)
{
    file_ops_register(&avif_ops);
}