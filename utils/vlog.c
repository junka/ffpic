#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <fnmatch.h>
#include <sys/queue.h>

#include "vlog.h"

struct vlog_dynamic_type {
    const char *name;
    uint32_t loglevel;
};

/** The vlog structure. */
static struct vlogs {
    uint32_t type;  /**< Bitfield with enabled logs. */
    uint32_t level; /**< Log level. */
    FILE *file;     /**< Output file set by vlog_openlog_stream, or NULL. */
    size_t dynamic_types_len;
    struct vlog_dynamic_type *dynamic_types;
} vlogs = {
    .type = ~0,
    .level = VLOG_DEBUG,
};

static struct log_cur_msg {
    uint32_t loglevel; /**< log level */
    uint32_t logtype;  /**< log type */
} log_cur_msg;

/* Stream to use for logging if vlogs.file is NULL */
static FILE *default_log_stream;

int
vlog_openlog_stream(FILE *f)
{
    vlogs.file = f;
    return 0;
}


/* Set global log level */
void
vlog_set_global_level(uint32_t level)
{
    vlogs.level = (uint32_t)level;
}

static FILE *
vlog_get_stream(void)
{
    FILE *f = vlogs.file;

    if (f == NULL) {
        /*
         * Grab the current value of stderr here, rather than
         * just initializing default_log_stream to stderr. This
         * ensures that we will always use the current value
         * of stderr, even if the application closes and
         * reopens it.
         */
        return default_log_stream ? : stderr;
    }
    return f;
}


/* Get global log level */
static uint32_t
vlog_get_global_level(void)
{
    return vlogs.level;
}

static int
vlog_get_level(uint32_t type)
{
    if (type >= vlogs.dynamic_types_len)
        return -1;

    return vlogs.dynamic_types[type].loglevel;
}


static bool
vlog_can_log(uint32_t logtype, uint32_t level)
{
    int log_level;

    if (level > vlog_get_global_level())
        return false;

    log_level = vlog_get_level(logtype);
    if (log_level < 0)
        return false;

    if (level > (uint32_t)log_level)
        return false;

    return true;
}

static int
_vlog(uint32_t level, uint32_t logtype, const char *format, va_list ap)
{
    FILE *f = vlog_get_stream();
    int ret;

    if (logtype >= vlogs.dynamic_types_len)
        return -1;
    if (!vlog_can_log(logtype, level))
        return 0;

    /* save loglevel and logtype in a global per-lcore variable */
    log_cur_msg.loglevel = level;
    log_cur_msg.logtype = logtype;

    ret = vfprintf(f, format, ap);
    fflush(f);
    return ret;
}

int
vlog(uint32_t level, uint32_t logtype, const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = _vlog(level, logtype, format, ap);
    va_end(ap);
    return ret;
}

static const char *
loglevel_to_string(uint32_t level)
{
    switch (level) {
    case 0: return "disabled";
    case VLOG_EMERG: return "emerg";
    case VLOG_ALERT: return "alert";
    case VLOG_CRIT: return "critical";
    case VLOG_ERR: return "error";
    case VLOG_WARNING: return "warning";
    case VLOG_NOTICE: return "notice";
    case VLOG_INFO: return "info";
    case VLOG_DEBUG: return "debug";
    default: return "unknown";
    }
}

/* dump global level and registered log types */
void
vlog_dump(FILE *f)
{
    size_t i;

    fprintf(f, "global log level is %s\n",
        loglevel_to_string(vlog_get_global_level()));

    for (i = 0; i < vlogs.dynamic_types_len; i++) {
        if (vlogs.dynamic_types[i].name == NULL)
            continue;
        fprintf(f, "id %zu: %s, level is %s\n",
            i, vlogs.dynamic_types[i].name,
            loglevel_to_string(vlogs.dynamic_types[i].loglevel));
    }
}

static int
vlog_lookup(const char *name)
{
    size_t i;

    for (i = 0; i < vlogs.dynamic_types_len; i++) {
        if (vlogs.dynamic_types[i].name == NULL)
            continue;
        if (strcmp(name, vlogs.dynamic_types[i].name) == 0)
            return i;
    }

    return -1;
}

static int
__vlog_register(const char *name, int id)
{
    char *dup_name = strdup(name);

    if (dup_name == NULL)
        return -1;

    vlogs.dynamic_types[id].name = dup_name;
    vlogs.dynamic_types[id].loglevel = VLOG_INFO;

    return id;
}

/* register an extended log type */
int
vlog_register(const char *name)
{
    struct vlog_dynamic_type *new_dynamic_types;
    int id, ret;

    id = vlog_lookup(name);
    if (id >= 0)
        return id;

    new_dynamic_types = realloc(vlogs.dynamic_types,
        sizeof(struct vlog_dynamic_type) *
        (vlogs.dynamic_types_len + 1));
    if (new_dynamic_types == NULL)
        return -1;
    vlogs.dynamic_types = new_dynamic_types;

    ret = __vlog_register(name, vlogs.dynamic_types_len);
    if (ret < 0)
        return ret;

    vlogs.dynamic_types_len++;

    return ret;
}

struct vlog_opt_loglevel {
    /** Next list entry */
    TAILQ_ENTRY(vlog_opt_loglevel) next;
    /** Compiled regular expression obtained from the option */
    regex_t re_match;
    /** Globbing pattern option */
    char *pattern;
    /** Log level value obtained from the option */
    uint32_t level;
};

TAILQ_HEAD(vlog_opt_loglevel_list, vlog_opt_loglevel);
static struct vlog_opt_loglevel_list opt_loglevel_list =
    TAILQ_HEAD_INITIALIZER(opt_loglevel_list);

int
vlog_register_type_and_pick_level(const char *name, uint32_t level_def)
{
    struct vlog_opt_loglevel *opt_ll;
    uint32_t level = level_def;
    int type;

    type = vlog_register(name);
    if (type < 0)
        return type;

    TAILQ_FOREACH(opt_ll, &opt_loglevel_list, next) {
        if (opt_ll->level > VLOG_DEBUG)
            continue;

        if (opt_ll->pattern) {
            if (fnmatch(opt_ll->pattern, name, 0) == 0)
                level = opt_ll->level;
        } else {
            if (regexec(&opt_ll->re_match, name, 0, NULL, 0) == 0)
                level = opt_ll->level;
        }
    }

    vlogs.dynamic_types[type].loglevel = level;

    return type;
}


void
vlog_init(void)
{
    vlog_set_global_level(VLOG_DEBUG);

    vlogs.dynamic_types = NULL;
    vlogs.dynamic_types_len = 0;

    default_log_stream = stdout;
}

void
vlog_uninit(void)
{
    if (vlogs.file != NULL) {
        fclose(vlogs.file);
    }
    for (int i = 0; i < vlogs.dynamic_types_len; i ++) {
        char *name = (char *)vlogs.dynamic_types[i].name;
        free(name);
    }
    free(vlogs.dynamic_types);
}