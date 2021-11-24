#ifndef _SVG_H_
#define _SVG_H_

#ifdef __cplusplus
extern "C" {
#endif

struct xml_attr {
    char *tag;
    char *value;
};

struct xml_node {
    struct xml_node *parent;
    char *tag;
    char *value;
    int attr_num;
    struct xml_attr *attr;
};

typedef struct {
    int height;
    int width;
    int path_num;

    struct xml_node * r;
} SVG;


void SVG_init(void);


#ifdef __cplusplus
}
#endif

#endif /*_SVG_H_*/
