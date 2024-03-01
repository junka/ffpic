#ifndef _BASEMEDIA_H_
#define _BASEMEDIA_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "byteorder.h"


#define FOURCC2UINT(a, b, c, d) (uint32_t)(a | b << 8 | c << 16 | d << 24)
#define TYPE2UINT(x) (uint32_t)(x[0]|x[1]<<8|x[2]<<16|x[3]<<24)

static inline char *
type2name(uint32_t type)
{
    static char name[5];
    name[4]= '\0';
    name[3] = (type >> 24) & 0xFF;
    name[2] = (type >> 16) & 0xFF;
    name[1] = (type >> 8) & 0xFF;
    name[0] = (type) & 0xFF;
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
    // uint8_t buf[0];
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

/* see 14496-12 8.3 ? 8.2.2.2 (2015) */
struct mvhd_box {
    FULL_BOX_ST;

    uint64_t ctime; // for version 0, it is 32bits
    uint64_t mtime; // for version 0, it is 32bits
    uint32_t timescale;
    uint64_t duration; // for version 0, it is 32bits

    uint32_t rate;
    uint16_t volume;
    uint16_t reserved;
    uint32_t rsd[2];
    uint32_t matrix[9];
    uint32_t pre_defined[6];
    uint32_t next_track_id;

};


/* see 8.9  handler, declares the metadata(handler) type */
struct hdlr_box {
    FULL_BOX_ST;

    uint32_t pre_defined;
    uint32_t handler_type;
    uint32_t reserved[3];
    char* name;
};

/*see 8.44.3 */
struct item_extent {

    uint64_t extent_index;

    uint64_t extent_offset;
    uint64_t extent_length;
};

struct item_location {
    uint16_t item_id;

    //valid when version 1, lsb 4 bits
    uint16_t construct_method;

    uint16_t data_ref_id;

    uint64_t base_offset;
    int16_t extent_count;
    struct item_extent * extents;
};

/* item location */
struct iloc_box {
    FULL_BOX_ST;
#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t length_size:4;   /* 0, 4, 8 */
    uint8_t offset_size:4;   /* 0, 4, 8 */
    uint8_t index_size:4; /* 0, 4, 8 */
    uint8_t base_offset_size:4;
#else
    uint8_t offset_size:4;   /* 0, 4, 8 */
    uint8_t length_size:4;   /* 0, 4, 8 */
    uint8_t base_offset_size:4; /* 0, 4, 8 */
    uint8_t index_size:4;
#endif
    uint16_t item_count;

    struct item_location *items;
};

/*see 8.44.4 primary item reference */
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

/* item infomation */
struct iinf_box {
    FULL_BOX_ST;
    uint32_t entry_count; // depends on version, 2 bytes of version 0, else 4 bytes
    struct infe_box *item_infos;
};

/* see 8.44.5 */
struct ipro_box {
    FULL_BOX_ST;
    uint16_t protection_count;
};

/* original format box */
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

/* scheme type box */
struct schm_box {
    FULL_BOX_ST;

    uint32_t scheme_type;
    uint32_t scheme_version;
    uint8_t * scheme_uri; //only exist when flag & 0x1
};

/* scheme information box */
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

/* pixel aspect ratio */
struct pasp_box {
    BOX_ST;

    uint32_t hSpacing;
    uint32_t vSpacing; 
};

/* clean aperture */
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

/* color information */
struct colr_box {
    BOX_ST;
    uint32_t color_type;
    uint16_t color_primaries;
    uint16_t transfer_characteristics;
    uint16_t matrix_coefficients;
#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t reserved:7;
    uint8_t full_range_flag:1;
#else
    uint8_t full_range_flag:1;
    uint8_t reserved:7;
#endif
};

//see 8.3.2 track header box
struct tkhd_box {
    FULL_BOX_ST;
    uint64_t creation_time; //for version 0 , size 32
    uint64_t modification_time; //for version 0 , size 32
    uint32_t track_id;
    uint32_t reserved;
    uint64_t duration; //for version 0 , size 32

    uint32_t rsd[2];
    int16_t layer;
    int16_t alternate_group;
    int16_t volume;
    uint16_t reserved0;
    uint32_t matrix[9];
    uint32_t width;
    uint32_t height;
};


struct mdhd_box {
    FULL_BOX_ST;
    uint64_t creation_time;     // for version 0 , size 32
    uint64_t modification_time; // for version 0 , size 32
    uint32_t timescale;
    uint64_t duration;  // for version 0 , size 32
    union {
        uint16_t pad:1;
        uint16_t language:15; //ISO-639-2/T, three bit-5 wide

        uint16_t lan;
    };
    uint16_t pre_defined;

};

struct SampleEntry {
    BOX_ST;
    uint8_t reserved[6];
    uint16_t data_reference_index;
};

struct stsd_box {
    FULL_BOX_ST;
    uint32_t entry_count;
    struct SampleEntry *entries;
};

struct stdp_box {
    FULL_BOX_ST;
    uint16_t *priority; //sample_count num
};

struct stsz_box {
    FULL_BOX_ST;
    uint32_t sample_size;
    uint32_t sample_count;
    uint32_t *entry_size;
};

struct stz2_box {
    FULL_BOX_ST;
    uint32_t reserved:24;
    uint32_t field_size:8;//4, 8, 16
    uint32_t sample_sount;
    uint16_t *entry_size; // real bits use filed_size
};

struct stts_box {
    FULL_BOX_ST;
    uint32_t entry_count;
    uint32_t *sample_count;
    uint32_t *sample_delta;
};

struct ctts_box {
    FULL_BOX_ST;
    uint32_t entry_count;
    uint32_t *sample_count;
    uint32_t *sample_offset;
};

struct stsc_box {
    FULL_BOX_ST;
    uint32_t entry_count;
    uint32_t *first_chunk;
    uint32_t *sample_per_chunk;
    uint32_t *sample_description_index;
};

struct stco_box {
    FULL_BOX_ST;
    uint32_t entry_count;
    uint32_t* chunk_offset;
};

struct stss_box {
    FULL_BOX_ST;
    uint32_t entry_count;

    uint32_t *sample_number;
};

struct stbl_box {
    BOX_ST;
    struct stsd_box stsd;
    union {
        struct stsz_box stsz;
        struct stz2_box stz2; // either one of these
    };
    struct stts_box stts;
    struct stsc_box stsc;
    struct stco_box stco;
    struct stss_box stss; //zero or one

    struct ctts_box ctts; //zero or one
    struct stdp_box stdp; //zero or one
};

// could be "url " or "urn "
struct DataEntryBox {
    FULL_BOX_ST;
    char *name;     // mandatory for urn, not exist for url
    char *location; // mandatory for url, optional for url
};

// see 8.7.2 (14496-12:2015)
struct dref_box {
    FULL_BOX_ST;
    uint32_t entry_count;
    struct DataEntryBox *entries;
};

struct dinf_box {
    BOX_ST;
    struct dref_box dref;
};


struct vmhd_box {
    FULL_BOX_ST;
    uint16_t graphicsmode;
    uint16_t opcolor[3];
};

struct smhd_box {
    FULL_BOX_ST;
    uint16_t balance;
    uint16_t reserved;
};


struct hmhd_box {
    FULL_BOX_ST;
    uint16_t maxPDUsize;
    uint16_t avgPDUsize;
    uint32_t maxbitrate;
    uint32_t avgbitrate;
    uint32_t reserved;
};

struct sthd_box {

};

struct nmhd_box {
    FULL_BOX_ST;

};


// see 8.4.4 (14496-12:2015)
// media information box
struct minf_box {
    BOX_ST;

    struct vmhd_box vmhd;
    struct smhd_box smhd;
    struct hmhd_box hmhd;
    struct sthd_box sthd;
    struct nmhd_box nmhd;

    struct dinf_box dinf;
    struct stbl_box stbl;
};

struct elng_box {
    FULL_BOX_ST;
    char *extended_language; // such as "en-US", "fr-FR", "zh-CN"
};

// see 8.4.1 (14496-12:2015)
struct mdia_box {
    BOX_ST;
    struct mdhd_box mdhd;
    struct hdlr_box hdlr;
    struct elng_box elng;//zero or one
    struct minf_box minf;
};


/* hint, cdsc, font, hind, vdep, vplx, subt */
struct track_ref_type_box {
    BOX_ST;
    uint32_t* track_ids;
};

// see 8.3.3
// track reference box
struct tref_box {
    BOX_ST;
    struct track_ref_type_box ref_type;
};


//see 8.11.12
struct itemtype_ref_box {
    BOX_ST;
    uint16_t from_item_id;
    uint16_t ref_count;
    uint16_t* to_item_ids;
};

/* item reference box */
struct iref_box {
    FULL_BOX_ST;
    int refs_count; // not in iso doc, add for conveniency
    struct itemtype_ref_box *refs;
};

/* see 8.1.1 */
struct mdat_box {
    BOX_ST;
    uint8_t *data;
};
/* see 8.1.2 */
struct skip_box {
    BOX_ST;
    uint8_t *data;
};

/* see IEC 23008-12 6.5.3 image spatial extents */
struct ispe_box {
    FULL_BOX_ST;
    uint32_t image_width;
    uint32_t image_height;
};

/* see ISO/IEC 23008-12  6.5.6 pixel information */
struct pixi_box {
    FULL_BOX_ST;
    uint8_t num_channels;
    uint8_t* bits_per_channel;
};

/* for HEIF may have hvcC, ispe */
/* for avif may have av1C, ispe, pixi, psap */
struct ipco_box {
    BOX_ST;
    struct box *property[4];
    int n_prop;
};


//item property association
struct ipma_item {
    uint32_t item_id;
    uint8_t association_count;
    uint16_t* association; // could be 1 byte or 2 bytes width, highest bit is always for seential
};

struct ipma_box {
    FULL_BOX_ST;
    uint32_t entry_count;
    struct ipma_item *entries; 
};


/* see iso_ico 230008-12 9.3 item properties */
struct iprp_box {
    BOX_ST;
    struct ipco_box ipco;
    struct ipma_box ipma;
};

struct trak_box {
    BOX_ST;
    struct tkhd_box tkhd;
    struct tref_box tref; // zero or one
    struct mdia_box mdia;
};

//see 8.10.1
struct udta_box {
    BOX_ST;

};

/*see 8.2.1.1 movie box*/
struct moov_box {
    BOX_ST;
    struct mvhd_box mvhd;
    struct meta_box *meta; //zero or one
    int trak_num;
    struct trak_box *trak; //one or more
    struct udta_box *udta; //zero or one
};

#pragma pack(pop)

uint32_t read_box(FILE *f, void * d, int len);
uint32_t read_full_box(FILE *f, void * d, int blen);

int read_ftyp(FILE *f, void *d);

void print_box(FILE *f, void *d);

void read_mvhd_box(FILE *f, struct mvhd_box *b);

void read_hdlr_box(FILE *f, struct hdlr_box *b);

void read_iloc_box(FILE *f, struct iloc_box *b);

void read_pitm_box(FILE *F, struct pitm_box *b);

void read_iinf_box(FILE *f, struct iinf_box *b);

void read_sinf_box(FILE *f, struct sinf_box *b);

void read_iref_box(FILE *f, struct iref_box *b);

void read_dinf_box(FILE *f, struct dinf_box *b);

typedef int (* read_box_callback)(FILE *f, struct box **b);

void read_iprp_box(FILE *f, struct iprp_box *b, read_box_callback cb);

/* read mdat */
void read_mdat_box(FILE *f, struct mdat_box *b);

void read_moov_box(FILE *f, struct moov_box *b);

#ifdef __cplusplus
}
#endif

#endif /*_BASEMEDIA_H_*/
