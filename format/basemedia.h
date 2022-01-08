#ifndef _BASEMEDIA_H_
#define _BASEMEDIA_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>


#define TYPE2UINT(x) (x[0]|x[1]<<8|x[2]<<16|x[3]<<24)

static char *
type2name(uint32_t type)
{
    char* name = malloc(5);
    char c1 = (type >> 24) & 0xFF;
    char c2 = (type >> 16) & 0xFF;
    char c3 = (type >> 8) & 0xFF;
    char c4 = (type) & 0xFF;
    sprintf(name, "%c%c%c%c", c4, c3, c2, c1);
    name[4]= '\0';
    return name;
}

#define UINT2TYPE(x) type2name(x)

#define FTYP TYPE2UINT("ftyp")
#define META TYPE2UINT("meta")
#define MDAT TYPE2UINT("mdat")
//prededined box type should be list in each file type header

#pragma pack(push, 2)

#define BOX_ST \
    uint32_t size; \
    uint32_t type

#define FULL_BOX_ST \
    uint32_t size;  \
    uint32_t type;  \
    uint32_t version: 8;    \
    uint32_t flags : 24


/* see 14496-12 4.2 */
struct box {
    BOX_ST;
    uint8_t buf[0];
};

struct full_box {
    FULL_BOX_ST;
};

/* see 14496-12 4.3 */
struct ftyp_box {
    uint32_t size;
    uint32_t major_brand;
    uint32_t minor_version;
    uint32_t *compatible_brands;
};

/* see 14496-12 8.2 */
struct mdat_box {
    BOX_ST;
    uint8_t *data;
};

/* see 14496-12 8.3 */
struct mvhd_box {
    FULL_BOX_ST;

    uint64_t ctime;
    uint64_t mtime;
    uint32_t timescale;
    uint64_t duration;

    uint32_t rate;
    uint16_t volume;
    uint16_t reserved;
    uint32_t rsd[2];
    uint32_t matrix[9];
    uint32_t pre_defined[6];
    uint32_t netx_track_id;

};


/* see 8.9 */
struct hdlr_box {
    FULL_BOX_ST;

    uint32_t pre_defined;
    uint32_t handler_type;
    uint32_t reserved[3];
    char* name;
};

/*see 8.44.3 */
struct item_extent {
    uint64_t extent_offset;
    uint64_t extent_length;
};

struct item_location {
    uint16_t item_id;
    uint16_t data_ref_id;
    
    uint64_t base_offset;
    int16_t extent_count;
    struct item_extent * extents;
};

struct iloc_box {
    FULL_BOX_ST;

    uint8_t offset_size:4;   /* 0, 4, 8 */
    uint8_t length_size:4;   /* 0, 4, 8 */
    uint8_t base_offset_size:4; /* 0, 4, 8 */
    uint8_t reserved:4;
    uint16_t item_count;

    struct item_location *items;
};

/*see 8.44.4 */
struct pitm_box {
    FULL_BOX_ST;

    uint16_t item_id;
};

/* see 8.44.6 */
struct infe_box {
    FULL_BOX_ST;

    uint16_t item_id;
    uint16_t item_protection_index;
    uint32_t item_type;

    char item_name[32];
    char content_type[32];
    char* content_encoding;
};

struct iinf_box {
    FULL_BOX_ST;
    uint16_t entry_count;
    struct infe_box *item_infos;
};

/* see 8.44.5 */
struct ipro_box {
    FULL_BOX_ST;
    uint16_t protection_count;
};


struct frma_box {
    BOX_ST;
    uint32_t data_format;
};

/* defined in 14496-1 */
// struct IPMP_Descriptor {
//     uint8_t tag;    //0x0B
//     uint8_t length;
//     uint8_t id;
//     uint16_t type;
//     uint8_t* data; //type = 0: url string; 1: n data
// };

// struct imif_box {
//     FULL_BOX_ST;
//     /* IPMP_Descriptor is defined in 14496-1. */
//     struct IPMP_Descriptor *ipmp_descs;
// };


struct schm_box {
    FULL_BOX_ST;

    uint32_t scheme_type;
    uint32_t scheme_version;
    uint8_t * scheme_uri; //only exist when flag & 0x1
};

struct schi_box {
    BOX_ST;
};

/* see 8.12 */
struct sinf_box {
    BOX_ST;
    struct frma_box original_format;
    // struct imif_box IPMP_descriptors;   //optional
    struct schm_box scheme_type_box;    //optional
    struct schi_box *info;    //optional
};

struct pasp_box {
    BOX_ST;

    uint32_t hSpacing;
    uint32_t vSpacing; 
};

struct clap_box {
    BOX_ST;

    uint32_t cleanApertureWidthN;
    uint32_t cleanApertureWidthD;

    uint32_t cleanApertureHeightN;
    uint32_t cleanApertureHeightD;

    uint32_t horizOffN;
    uint32_t horizOffD;

    uint32_t vertOffN;
    uint32_t vertOffD;
};

struct colr_box {
    BOX_ST;
    uint32_t color_type;
    uint16_t color_primaries;
    uint16_t transfer_characteristics;
    uint16_t matrix_coefficients;
    uint8_t full_range_flag:1;
    uint8_t reserved:7;
};

//see 8.3.2 track header box
struct tkhd_box {
    FULL_BOX_ST;
    uint64_t creation_time; //for version 0 , size 32
    uint64_t modification_time; //for version 0 , size 32
    uint32_t track_id;
    uint32_t reserved;
    uint64_t duration; //for version 0 , size 32

    uint64_t rsd;
    int16_t layer;
    int16_t alternate_group;
    int16_t volume;
    uint32_t matrix[9];
    uint32_t width;
    uint32_t height;
};


//see 8.3.3
//track reference box
struct tref_box {
    BOX_ST;
    
};

/* hint, cdsc, hind, vdep, vplx */
struct track_ref_type_box {
    BOX_ST;
    uint32_t* track_ids;
};


//see 8.11.12
struct itemtype_ref_box {
    BOX_ST;
    uint16_t from_item_id;
    uint16_t ref_count;
    uint16_t* to_item_ids;
};

struct iref_box {
    FULL_BOX_ST;
    struct itemtype_ref_box *refs;
};

#pragma pack(pop)

uint32_t read_box(FILE *f, void * d, int len);
uint32_t read_full_box(FILE *f, void * d, int blen);

void read_ftyp(FILE *f, void *d);

void print_box(FILE *f, void *d);

void read_mvhd_box(FILE *f, struct mvhd_box *b);

void read_hdlr_box(FILE *f, struct hdlr_box *b);

void read_iloc_box(FILE *f, struct iloc_box *b);

void read_pitm_box(FILE *F, struct pitm_box *b);

void read_iinf_box(FILE *f, struct iinf_box *b);

void read_sinf_box(FILE *f, struct sinf_box *b);

void read_iref_box(FILE *f, struct iref_box *b);

#ifdef __cplusplus
}
#endif

#endif /*_BASEMEDIA_H_*/