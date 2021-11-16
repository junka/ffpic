#ifndef _JP2_H_
#define _JP2_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "basemedia.h"


//following jp box and ftyp box, super box
#define JP2H TYPE2UINT("jp2h")


//other jp2 boxes
#define IHDR TYPE2UINT("ihdr")
//color specification
#define COLR TYPE2UINT("colr")
//bits per component
#define BPCC TYPE2UINT("bpcc")
//palette
#define PCLR TYPE2UINT("pclr")
//component mapping
#define CMAP TYPE2UINT("cmap")
//channel definition
#define CDEF TYPE2UINT("cdef")
//resolution
#define RES TYPE2UINT("res ")
//capture resolution
#define RESC TYPE2UINT("resc")
//default display resolution
#define RESD TYPE2UINT("resd")
//contiguous code stream
#define JP2C TYPE2UINT("jp2c")
//intellectual property
#define JP2I TYPE2UINT("jp2i")

#define XML TYPE2UINT("xml ")
#define UUID TYPE2UINT("uuid")
#define UINF TYPE2UINT("uinf")
#define ULST TYPE2UINT("ulst")
#define URL TYPE2UINT("url ")



#define _JP2_MARKER(x, v) (v<<8|x)

#define MARK(v) _JP2_MARKER(0xFF, v)

#define SOC MARK(0x4F)
#define SOT MARK(0x90)
#define SOD MARK(0x93)
#define EOC MARK(0xD9)

#define CAP MARK(0x50)

#define SIZ MARK(0x51)

#define COD MARK(0x52)
#define COC MARK(0x53)

#define PRF MARK(0x56)

#define QCD MARK(0x5C)
#define QCC MARK(0x5D)
#define RGN MARK(0x5E)
#define POC MARK(0x5F)

#define TLM MARK(0x55)
#define PLM MARK(0x57)
#define PLT MARK(0x58)
#define PPM MARK(0x60)
#define PPT MARK(0x61)

#define CRG MARK(0x63)

//COM in 2019
#define CME MARK(0x64) 

#define SOP MARK(0x91)
#define EPH MARK(0x92)


#pragma pack(push, 1)

//first box, JP signature box
struct jp2_signature_box {
    uint32_t len;
    uint32_t major_type;
    uint32_t minor_type;
};

struct jp2_component {
    uint32_t depth;
    uint32_t sgnd;
    uint32_t bpcc;
};

struct jp2_ihdr {
    uint32_t width;
    uint32_t height;
    uint16_t num_comp;
    uint8_t bpc;
    uint8_t c;
    uint8_t unkc;
    uint8_t ipr;

    struct jp2_component *comps;
};

struct jp2_icc_profile {
    uint8_t* iccp;
    int iccplen;
};

enum color_method {
    ENUMERATED_COLORSPACE = 1,
    RESTRICT_ICC_PROFILE = 2,
};

struct jp2_colr {
    uint8_t method;     /* see enum color_method */
    uint8_t precedence; /* reserved for iso use, set to zero */
    uint8_t approx;     /*  */

    //may have part
    uint32_t enum_cs;
    struct jp2_icc_profile iccpro;

};

/**
Component mappings: channel index, mapping type, palette index
*/
struct jp2_cmap_component {
    uint16_t channel_id;
    uint8_t mapping_type;
    uint8_t palette_id;  
};

struct jp2_cmap {

};

struct pclr_chan {
    uint8_t sign:1;
    uint8_t size:7;
};

struct jp2_pclr {
    uint16_t num_entry;
    uint8_t num_channel;
    struct pclr_chan * channels;
    uint32_t *entries;
    // struct jp2_cmap_component * cmap;
};


struct jp2_cdef_chan {
    uint16_t chan_id;
    uint16_t type;
    uint16_t assoc;
};

struct jp2_cdef {
    uint16_t num_chans;
    struct jp2_cdef_chan * ents;
};


struct jp2h_box {
    struct jp2_ihdr ihdr;
    struct jp2_colr colr;
    struct jp2_cmap cmap;
    struct jp2_cdef cdef;
    struct jp2_pclr pclr;
};


struct jp2c_box {

};

struct jp2_xml {
    uint8_t * data;
};

struct jp2_uuid {
    uint64_t id[2];
    uint8_t *data;
};

struct jp2_url {
    uint32_t version : 8;
    uint32_t flags : 24;
    uint8_t *location;
};

struct jp2_ulst {
    uint16_t num_uuid;
    uint64_t* id[2];
};

struct jp2_uinf {
    struct jp2_ulst ulst;
    struct jp2_url url;
};

struct sop {
    uint16_t length;
    uint16_t seq_num;
};

struct sot {
    uint16_t length;
    uint16_t tile_id;
    uint32_t tile_size;
    uint8_t tile_part_index;
    uint8_t tile_part_nums;

    uint32_t offset_start;
};

struct scomponent {
    uint8_t depth;
    uint8_t horizontal_separation;
    uint8_t verticcal_separation;
};

struct siz {
    uint16_t length;
    uint16_t cap;
    uint32_t width;
    uint32_t height;
    uint32_t left;
    uint32_t top;
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t tile_left;
    uint32_t tile_top;
    uint16_t component_num;
    struct scomponent *comps;
};

struct cap {
    uint16_t length;
    uint32_t bitmap;
    uint16_t* extensions;
};

struct prf {
    uint16_t length;
    uint16_t *profile;
};


enum PROGRESSION {
    PROGRESSION_LV_R_LV_COMP_POS = 0,
    PROGRESSION_R_LV_LAYER_COMP_POS = 1,
    PROGRESSION_R_LV_POS_COMP_LAYER = 2,
    PROGRESSION_POS_COMP_R_LV_LAYER = 3,
    PROGRESSION_COMP_POS_R_LV_LAYER = 4,
};

struct coding_para {
    uint8_t decomp_level_num;
    uint8_t code_block_width;
    uint8_t code_block_height;
#ifdef __LITTLE_ENDIAN__
    uint8_t selctive_arithmetic:1;
    uint8_t reset_on_boundary:1;
    uint8_t termination:1;
    uint8_t vertical_context:1;
    uint8_t predictable_termination:1;
    uint8_t segmentation_symbol:1;
    uint8_t rsd:2;
#else
    uint8_t rsd:2;
    uint8_t segmentation_symbol:1;
    uint8_t predictable_termination:1;
    uint8_t vertical_context:1;
    uint8_t termination:1;
    uint8_t reset_on_boundary:1;
    uint8_t selctive_arithmetic:1;
#endif
    uint8_t transform;

    uint8_t* precinct_size;
};
struct cod {
    uint16_t length;

#ifdef __LITTLE_ENDIAN__
    uint8_t entropy: 1;
    uint8_t sop: 1;
    uint8_t eph: 1;
    uint8_t resv: 5;
#else
    uint8_t resv: 5;
    uint8_t eph: 1;
    uint8_t sop: 1;
    uint8_t entropy: 1;
#endif
    uint8_t progression_order;  /* see enum PROGRESSION */
    uint16_t layers_num;
    uint8_t multiple_transform; /* last bit valid */

    struct coding_para p;
};


struct coc {
    uint16_t length;
    uint16_t comp_num; //8 bits or 16 bis depends on depth
    uint8_t resv: 7;
    uint8_t entropy: 1;
    struct coding_para p;
};

struct rgn {
    uint16_t length;
    uint16_t comp_num; //8 bits or 16 bis depends on depth
    uint8_t roi_style;
    uint8_t* roi_shift;
};

//Quantization default
struct qcd {
    uint16_t length;
#if __LITTLE_ENDIAN__
    uint8_t guard_num:3;
    uint8_t quant_type:5;
#else
    uint8_t quant_type:5;
    uint8_t guard_num:3;
#endif
    uint8_t *table;
};

//Quantization component
struct qcc {
    uint16_t length;
    uint16_t comp_id; //8 bits or 16 bis depth
    uint8_t quant_style;
    uint8_t *table;
};

//progression order change
struct poc {
    uint16_t length;

};

struct cme {
    uint16_t length;
    uint16_t use;
    uint8_t *str;
};





#pragma pack(pop)

struct main_header {
    struct siz siz;
    struct cap cap;
    struct prf prf;
    struct cod cod;
    struct coc coc;
    struct qcd qcd;
    struct qcc qcc;
    struct rgn rgn;
    struct cme cme;
};

struct tile_header {
    struct sot sot;
    struct sop sop;
};

typedef struct {
    struct ftyp_box ftyp;
    struct jp2h_box jp2h;
    struct jp2c_box *jp2c;
    struct jp2_uuid uuid;
    int xml_num;
    struct jp2_xml* xml;
    int uinf_num;
    struct jp2_uinf* uuid_info;

    struct main_header main_h;
    // struct tile_header tile_h;
    int tile_nums;
    struct tile_header* tiles;

    uint8_t *data;
} JP2;


void JP2_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_JP2_H_*/