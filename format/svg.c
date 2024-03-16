#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "file.h"
#include "utils.h"
#include "svg.h"


static char
read_skip_blank(FILE *f)
{
    char s = fgetc(f);
    while (!feof(f) && (s == '\t' || s == '\r' || s == '\n')) {
        s = fgetc(f);
    }
    return s;
}

static char
read_skip_space(FILE *f)
{
    char s = read_skip_blank(f);
    while (!feof(f) && isspace(s)) {
        s = read_skip_blank(f);
    }
    return s;
}

static int
read_till_space(FILE *f, char *s)
{
    int i = 0;
    char c = fgetc(f);
    while (c != ' ') {
        s[i ++] = c;
        c = fgetc(f);
    }
    s[i] = '\0';
    return i;
}

static int UNUSED
read_context(FILE *f, char *buf)
{
    read_skip_space(f);
    fseek(f, -1, SEEK_CUR);
    int len = read_till_space(f, buf);
    return len;
}

static struct xml_node *
xml_node_new(struct xml_node *parent)
{
    struct xml_node *n = malloc(sizeof(struct xml_node));
    n->attr_num = 0;
    n->parent = parent;
    n->tag = NULL;
    n->value = NULL;
    return n;
}

static void
xml_node_free(struct xml_node *n)
{
    if (n->tag)
        free(n->tag);
    if (n->value)
        free(n->value);
    for (int i = 0; i < n->attr_num; i ++) {
        free(n->attr[i].tag);
        free(n->attr[i].value);
    }
    free(n->attr);
    if (n->parent) {
        xml_node_free(n->parent);
    }
    free(n);
}

static int
read_attrs(FILE *f, struct xml_node* n, char endch)
{
    enum attr_state {
        PRE_KEY,
        KEY,
        PRE_VALUE,
        VALUE,
        END,
    } state = PRE_KEY;
    char value_end = '\"';
    // char s = read_skip_space(f);
    char s;
    struct xml_attr * attr = NULL;
    n->attr = malloc(sizeof(struct xml_attr));

    fseek(f, -1 , SEEK_CUR);
    long start;


    while (!feof(f)) {
        s = fgetc(f);
        switch (state)
        {
            case PRE_KEY:
                if (s == endch || s == '>') {
                    state = END;
                } else if (!isspace(s)) {
                    state = KEY;
                    start = ftell(f) - 1;
                }
                break;
            case KEY:
                if (s == '=') {
                    state = PRE_VALUE;
                    int l = ftell(f) - 1 - start;
                    n->attr = realloc(n->attr, sizeof(struct xml_attr) * (n->attr_num + 1));
                    attr = &n->attr[n->attr_num ++];
                    attr->tag = malloc(l+1);
                    fseek(f, -l-1, SEEK_CUR);
                    fread(attr->tag, l, 1, f);
                    attr->tag[l] = '\0';
                }
                break;
            case PRE_VALUE:
                if (s == '\"' || s == '\'') {
                    state = VALUE;
                    value_end = s;
                    start = ftell(f) - 1;
                }
                break;
            case VALUE:
                if (s == value_end) {
                    state = PRE_KEY;
                    int l = ftell(f) - start;
                    attr->value = malloc(l+1);
                    fseek(f, -l, SEEK_CUR);
                    fread(attr->value, l, 1, f);
                    attr->value[l] = '\0';
                }
                break;
            default:
                break;
        }

        if (state == END) {
            break;
        }
    }

    return 0;
}

static int
read_start_tag(struct xml_node * n, FILE *f)
{
    enum tag_state {
        NAME,
        ATTR,
        END,
    } state = NAME;

    int len;
    char s = '\0';

    fseek(f, -1 , SEEK_CUR);
    long start = ftell(f);

    while(!feof(f)) {
        s = fgetc(f);
        switch(state) {
            case NAME:
                if (isspace(s) || s == '>' || s == '/') {
                    state = (s != '>' && s != '/') ? ATTR : END;
                    if (state == ATTR) {
                        len = ftell(f) -1 - start;
                        n->tag = malloc(len);
                        fseek(f, -len-1, SEEK_CUR);
                        fread(n->tag, len, 1, f);
                        n->tag[len] = '\0';
                    }
                }
                break;
            case ATTR:
                read_attrs(f, n, '/');
                state = END;
                break;
            default:
                break;
        }
        if (state == END) {
            break;
        }
    }
    while (s != '>') {
        s = read_skip_space(f);
    }

    // printf("start\n");
    // printf("%s:\n", n->tag);
    // for (int i = 0; i < n->attr_num; i ++) {
    //     printf("\t %s: %s\n", n->attr[i].tag, n->attr[i].value);
    // }
    return 0;
}

static int
read_ins(struct xml_node * n, FILE *f)
{
    enum ins_state {
        NAME,
        ATTR,
        END,
    } state = NAME;

    int len;
    char s = '\0';

    fseek(f, -1, SEEK_CUR);
    long start = ftell(f);

    while(!feof(f)) {
        s = fgetc(f);
        switch(state) {
            case NAME:
                if (isspace(s) || s == '>') {
                    len = ftell(f) - start;
                    n->tag = malloc(len);
                    fseek(f, -len, SEEK_CUR);
                    fread(n->tag, len, 1, f);
                    state = (s != '>') ? ATTR : END;
                }
                break;
            case ATTR:
                state = END;
                read_attrs(f, n, '?');
                break;
            default:
                break;
        }
        if (state == END) {
            break;
        }
    }
    while (s != '>') {
        s = read_skip_space(f);
    }

    // printf("%s:\n", n->tag);
    // for (int i = 0; i < n->attr_num; i ++) {
    //     printf("\t %s: %s\n", n->attr[i].tag, n->attr[i].value);
    // }

    return 0;
}


static int
read_dtd(struct xml_node * n, FILE *f)
{
    enum dtd_state {
        TYPE,
        NOTE,
        PRE_SYSTEM,
        SYSTEM,
        PRE_DTD,
        DTD,
        END,
    } state = TYPE;

    int len;
    char s = '\0';

    fseek(f, -1, SEEK_CUR);
    long start = ftell(f);

    while(!feof(f)) {
        s = fgetc(f);
        switch(state) {
            case TYPE:
                if (isspace(s)) {
                    start = ftell(f);
                    state = NOTE;
                }
                break;
            case NOTE:
                if (isspace(s)) {
                    len = ftell(f) - 1 - start;
                    n->tag = malloc(len);
                    fseek(f, -len - 1, SEEK_CUR);
                    fread(n->tag, len, 1, f);
                    state = PRE_SYSTEM;
                }
                break;
            case PRE_SYSTEM:
                if (s == '\"') {
                    state = SYSTEM;
                }
                break;
            case SYSTEM:
                if (s == '\"') {
                    state = PRE_DTD;
                }
                break;
            case PRE_DTD:
                if (s == '\"') {
                    state = DTD;
                }
                break;
            case DTD:
                if (s == '\"') {
                    state = END;
                }
                break;
            default:
                break;
        }
        if (state == END) {
            break;
        }
    }

    while (s != '>') {
        s = read_skip_space(f);
    }

    // printf("DTD\n");
    // printf("%s:\n", n->tag);
    // for (int i = 0; i < n->attr_num; i ++) {
    //     printf("\t %s: %s\n", n->attr[i].tag, n->attr[i].value);
    // }

    return 0;
}


static int
read_comm(FILE *f)
{
    enum comm_state {
        COMMENT,
        MINUS,
        MINUS2,
    } state = COMMENT;

    // int len;
    char s = '\0';
    
    fseek(f, -1 , SEEK_CUR);

    while(!feof(f)) {
        s = fgetc(f);
        switch(state) {
            case COMMENT:
                if (s == '-') {
                    state = MINUS;
                }
                break;
            case MINUS:
                if (s == '-') {
                    state = MINUS2;
                } else {
                    state = COMMENT;
                }
                break;
            case MINUS2:
                if (s == '>') {
                    return 0;
                } else {
                    state = COMMENT;
                }
                break;
            default:
                break;
        }

    }
    while (s != '>') {
        s = read_skip_space(f);
    }
    return 0;
}

static int
read_end_tag(FILE *f)
{
    char s;

    fseek(f, -1 , SEEK_CUR);

    while(!feof(f)) {
        s = fgetc(f);
        if (s == '>') {
            break;
        }
    }
    return 0;
}

static int
read_text(FILE *f)
{
    char s;
    // long start = ftell(f) - 1;

    fseek(f, -1 , SEEK_CUR);
    while (!feof(f)) {
        s = fgetc(f);
        if (s == '<') {
            fseek(f, -1 , SEEK_CUR);
            return 0;
        }
    }
    return 0;
}

static int
read_xml(SVG *g, FILE *f)
{
    enum xml_state {
        NONE = 0,
        LT,
        START,
        TEXT,
        END,
        INS,
        PRE_COMM,
        PRE_COMM2,
        COMM,
        DTD,
    } state = NONE;

    char s;
    // int len;
    struct xml_node *root = NULL;
    struct xml_node *n = NULL;

    while(!feof(f)) {
        s = getc(f);
        switch (state) {
            case NONE:
                // printf("none %c\n", s);
                if (s == '<') {
                    state = LT;
                } else if (!isspace(s)) {
                    state = TEXT;
                }
                break;
            case LT:
                if (s == '?') {
                    state = INS;
                } else if (s == '/') {
                    state = END;
                } else if (s == '!') {
                    state = PRE_COMM;
                } else if (isalpha(s) || s == '_') {
                    state = START;
                    fseek(f, -1, SEEK_CUR);
                }
                break;
            case START:
                n = xml_node_new(root);
                read_start_tag(n, f);
                state = NONE;
                break;
            case INS:
                n = xml_node_new(root);
                read_ins(n, f);
                state = NONE;
                break;
            case END:
                read_end_tag(f);
                state = NONE;
                break;
            case TEXT:
                read_text(f);
                state = NONE;
                break;
            case PRE_COMM:
                if (s == '-') {
                    state = PRE_COMM2;
                } else if (s =='D') {
                    state = DTD;
                }
                break;
            case PRE_COMM2:
                if (s == '-') {
                    state = COMM;
                }
                break;
            case COMM:
                read_comm(f);
                state = NONE;
                break;
            case DTD:
                n = xml_node_new(root);
                read_dtd(n, f);
                state = NONE;
            default:
                break;
        }
        root = n;
    }
    g->r = n;
    return 0;
}


static int
SVG_probe(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    char buf[2048];
    fgets(buf, 2048, f);
    if (!strncmp("<?xml version=\"1.0\" standalone=\"no\"?>", buf, 37)) {
        fgets(buf, 2048, f);
        if (!strncmp("<!DOCTYPE svg PUBLIC ", buf, 21)) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return -EINVAL;
}


static struct pic* 
SVG_load(const char *filename, int skip_flag UNUSED)
{
    struct pic * p = pic_alloc(sizeof(SVG));
    SVG * s = p->pic;
    FILE *f = fopen(filename, "r");
    read_xml(s, f);

    return p;
}

static void 
SVG_free(struct pic *p)
{
    SVG * s = (SVG *)p->pic;
    xml_node_free(s->r);
    pic_free(p);
}

static void
SVG_info(FILE *f, struct pic* p UNUSED)
{
    fprintf(f, "SVG file formart\n");
}


static struct file_ops svg_ops = {
    .name = "SVG",
    .probe = SVG_probe,
    .load = SVG_load,
    .free = SVG_free,
    .info = SVG_info,
};

void 
SVG_init(void)
{
    file_ops_register(&svg_ops);
}
