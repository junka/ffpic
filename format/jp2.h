#ifndef _JP2_H_
#define _JP2_H_

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#include "basemedia.h"

//following jp box and ftyp box, super box
#define JP2H FOURCC2UINT('j', 'p', '2', 'h')

#define PRFL FOURCC2UINT('p', 'r', 'f', 'l')

//other jp2 boxes
#define IHDR FOURCC2UINT('i', 'h', 'd', 'r')
//color specification
#define COLR FOURCC2UINT('c', 'o', 'l', 'r')
//bits per component
#define BPCC FOURCC2UINT('b', 'p', 'c', 'c')
//palette
#define PCLR FOURCC2UINT('p', 'c', 'l',  'r')
//component mapping
#define CMAP FOURCC2UINT('c', 'm', 'a', 'p')
//channel definition
#define CDEF FOURCC2UINT('c', 'd', 'e', 'f')
//resolution
#define RES  FOURCC2UINT('r', 'e', 's', ' ')
//capture resolution
#define RESC FOURCC2UINT('r', 'e', 's', 'c')
//default display resolution
#define RESD FOURCC2UINT('r', 'e', 's', 'd')
//contiguous code stream
#define JP2C FOURCC2UINT('j', 'p', '2', 'c')
//intellectual property
#define JP2I FOURCC2UINT('j', 'p', '2', 'i')

#define XML  FOURCC2UINT('x', 'm', 'l', ' ')
#define UUID FOURCC2UINT('u', 'u', 'i', 'd')
#define UINF FOURCC2UINT('u', 'i', 'n', 'f')
#define ULST FOURCC2UINT('u', 'l', 's', 't')
#define URL  FOURCC2UINT('u', 'r', 'l', ' ')



#define _JP2_MARKER(x, v) (v<<8|x)

#define MARK(v) _JP2_MARKER(0xFF, v)
//start of code stream
#define SOC MARK(0x4F)
// end of code stream
#define EOC MARK(0xD9)
// extended capabilities marker
#define CAP MARK(0x50)
// image and tile size marker
#define SIZ MARK(0x51)
// coding style default marker
#define COD MARK(0x52)
// coding style component marker
#define COC MARK(0x53)

#define PRF MARK(0x56)
// quantation default marker
#define QCD MARK(0x5C)
// quantation component marker
#define QCC MARK(0x5D)
#define RGN MARK(0x5E)
#define POC MARK(0x5F)

#define TLM MARK(0x55)
// packet length, main header marker
#define PLM MARK(0x57)
// packet length, tile-part header marker
#define PLT MARK(0x58)
#define PPM MARK(0x60)
#define PPT MARK(0x61)

#define CRG MARK(0x63)

// Comment marker, which is COM in 2019
#define CME MARK(0x64)

// start of tile-part marker
#define SOT MARK(0x90)
// start of packet marker
#define SOP MARK(0x91)
// end of packett header marker
#define EPH MARK(0x92)
// start of data marker
#define SOD MARK(0x93)

#pragma pack(push, 1)

// I.6 Box definition share the same struct with basemedia
// LBox TBox XLBox DBox, which could use BOX_ST

//first box, JP signature box
struct jp2_signature_box {
    BOX_ST;
    uint32_t magic; //0x0D0A870A
};

// see ISO/IEC 15444-1
struct jp2_bpcc_box {
    BOX_ST;
    uint8_t *bits_per_comp;//low 7bits as value, high 1bit as sign
};

// see ISO/IEC 15444-1
struct jp2_ihdr_box {
    BOX_ST;
    uint32_t height;
    uint32_t width;
    uint16_t num_comp;

    uint8_t bpc;    //bits per component
    uint8_t c;      //compression type
    uint8_t unkc;   //unkown colorspace
    uint8_t ipr;    //intellectual property
};

struct jp2_icc_profile {
    uint8_t* iccp;
    int iccplen;
};

enum color_method {
    ENUMERATED_COLORSPACE = 1,
    RESTRICT_ICC_PROFILE = 2,
};

struct jp2_colr_box {
    BOX_ST;
    uint8_t method;     /* see enum color_method */
    uint8_t precedence; /* reserved for iso use, set to zero */
    uint8_t approx;     /* approximation set to zero */

    //may have part
    uint32_t enum_cs;   /* exist when method is 1, valid value is 16, 17, 18*/
    struct jp2_icc_profile iccpro; /* exist when method is 2*/

};

/**
Component mappings: channel index, mapping type, palette index
*/
struct jp2_cmap_component {
    uint16_t channel_id;
    uint8_t mapping_type;
    uint8_t palette_id;  
};

// struct jp2_cmap {

// };

struct jp2_pclr_box {
    BOX_ST;
    uint16_t num_entry;
    uint8_t num_channel;
    uint16_t palette_input; //
    uint16_t *component;
    uint8_t *depth;
    uint32_t *entries;
    // struct jp2_cmap_component * cmap;
};

// capture resolution box
struct jp2_resc_box {
    BOX_ST;
    uint16_t vrcn;
    uint16_t vrcd;
    uint16_t hrcn;
    uint16_t hrcd;
    uint8_t vrce;
    uint8_t hrce;
};

struct jp2_resd_box {
    BOX_ST;
    uint16_t vrdn;
    uint16_t vrdd;
    uint16_t hrdn;
    uint16_t hrdd;
    uint8_t vrde;
    uint8_t hrde;
};

struct jp2_res_box {
    BOX_ST;
    struct jp2_resc_box resc;
    struct jp2_resd_box resd;
};

struct jp2_cdef_box {
    BOX_ST;
    uint16_t num_comp;
    uint16_t *comp_id;
    uint16_t *comp_type;
    uint16_t *comp_assoc;
};


struct jp2h_box {
    BOX_ST;
    struct jp2_ihdr_box ihdr; // required
    struct jp2_bpcc_box bpcc; // optional

    int n_colr;
    struct jp2_colr_box *colr; // required

    struct jp2_pclr_box pclr; // optional
    struct jp2_cdef_box cdef; // optional
    struct jp2_res_box res; //optional

};


// struct jp2c_box {

// };

//see I.7.1 xml boxes
struct jp2_xml_box {
    BOX_ST;
    uint8_t * data;
};

typedef struct {
    uint64_t v[2];
} uint128_t;

struct jp2_uuid_box {
    uint128_t id;
    uint8_t *data;
};

struct jp2_url_box {
    FULL_BOX_ST;
    char *location;
};

// see I.7.3.1
struct jp2_ulst_box {
    BOX_ST;
    uint16_t num_uuid;
    uint128_t* id;
};

// see I.7.3
struct jp2_uinf_box {
    BOX_ST;
    struct jp2_ulst_box ulst;
    struct jp2_url_box url;
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

struct ppt {
    uint16_t length;
    uint8_t index;
    uint8_t *data;
};

struct scomponent {
    uint8_t depth;
    uint8_t horizontal_separation;
    uint8_t vertical_separation;
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

struct jp2_prfl_box {
    BOX_ST;
    uint32_t brand;
    uint32_t *compatibility_list;
};


enum PROGRESSION {
    PROGRESSION_LRCP = 0,
    PROGRESSION_RLCP = 1,
    PROGRESSION_RPCL = 2,
    PROGRESSION_PCRL = 3,
    PROGRESSION_CPRL = 4,
};

//see table A.15
struct coding_para {
    uint8_t decomp_level_num;
    uint8_t code_block_width;
    uint8_t code_block_height;
#if BYTE_ORDER == LITTLE_ENDIAN
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

// see A.6.1
struct cod {
    uint16_t length;

#if BYTE_ORDER == LITTLE_ENDIAN
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
#if BYTE_ORDER == LITTLE_ENDIAN
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

//progression order change, in spec it is POD
struct poc {
    uint16_t length;
    uint8_t res_start_index;
    uint16_t comp_start_index;  // 8bit when Csiz<257; 16bit when Csiz >= 257
    uint16_t layer_end_index;
    uint8_t res_end_index;
    uint16_t comp_end_index;    // 8bit when Csiz<257; 16bit when Csiz >= 257
    uint8_t progression_order;
};

struct ppm {
    uint16_t length;
    uint8_t index;
    uint32_t code_size;
    uint8_t *data;
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
    // struct prf prf;
    struct cod cod;
    struct coc coc;
    struct qcd qcd;
    struct qcc qcc;
    struct poc poc;
    struct rgn rgn;
    struct ppm ppm;
    struct cme cme;
};

struct tile_header {
    struct sot sot;
    // struct sop sop;
    struct ppt ppt;
};

typedef struct {
    struct ftyp_box ftyp;
    struct jp2_prfl_box prfl;
    struct jp2h_box jp2h;
    // struct jp2c_box *jp2c;
    struct jp2_uuid_box uuid;
    int xml_num;
    struct jp2_xml_box* xml;
    int uinf_num;
    struct jp2_uinf_box* uinf;

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
