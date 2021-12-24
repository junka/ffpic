
#ifndef _VLOG_H_
#define _VLOG_H_

#ifdef __cplusplus
extern "C" {
#endif



#define _format_printf(format_index, first_arg) \
	__attribute__((format(printf, format_index, first_arg)))

/**
 * Change the stream that will be used by the logging system.
 *
 * This can be done at any time. The f argument represents the stream
 * to be used to send the logs. If f is NULL, the default output is
 * used (stderr).
 *
 * @param f
 *   Pointer to the stream.
 * @return
 *   - 0 on success.
 *   - Negative on error.
 */
int vlog_openlog_stream(FILE *f);

/**
 * Set the global log level.
 *
 * After this call, logs with a level lower or equal than the level
 * passed as argument will be displayed.
 *
 * @param level
 *   Log level. A value between VLOG_EMERG (1) and VLOG_DEBUG (8).
 */
void vlog_set_global_level(uint32_t level);


/**
 * Register a dynamic log type
 *
 * If a log is already registered with the same type, the returned value
 * is the same than the previous one.
 *
 * @param name
 *   The string identifying the log type.
 * @return
 *   - >0: success, the returned value is the log type identifier.
 *   - (-ENOMEM): cannot allocate memory.
 */
int vlog_register(const char *name);


int vlog(uint32_t level, uint32_t logtype, const char *format, ...)
    _format_printf(3, 4);


/* Can't use 0, as it gives compiler warnings */
#define VLOG_EMERG    1U  /**< System is unusable.               */
#define VLOG_ALERT    2U  /**< Action must be taken immediately. */
#define VLOG_CRIT     3U  /**< Critical conditions.              */
#define VLOG_ERR      4U  /**< Error conditions.                 */
#define VLOG_WARNING  5U  /**< Warning conditions.               */
#define VLOG_NOTICE   6U  /**< Normal but significant condition. */
#define VLOG_INFO     7U  /**< Informational.                    */
#define VLOG_DEBUG    8U  /**< Debug-level messages.             */

int vlog_register_type_and_pick_level(const char *name, uint32_t level_def);

#define VLOG(l, t, ...)					    \
	 vlog(VLOG_ ## l,					\
		vlog_## t, # t ": " __VA_ARGS__)

#define VDBG(t, ...)   VLOG(DEBUG, t, __VA_ARGS__)
#define VINFO(t, ...)   VLOG(INFO, t, __VA_ARGS__)
#define VNOTE(t, ...)   VLOG(NOTICE, t, __VA_ARGS__)
#define VWARN(t, ...)   VLOG(WARNING, t, __VA_ARGS__)
#define VERR(t, ...)   VLOG(ERR, t, __VA_ARGS__)
#define VCRIT(t, ...)   VLOG(CRIT, t, __VA_ARGS__)
#define VABORT(t, ...)   VLOG(ALERT, t, __VA_ARGS__)
#define VFATAL(t, ...)   VLOG(EMERG, t, __VA_ARGS__)


#define VLOG_REGISTER(name, level)				\
int vlog_##name;								        \
static void __attribute__((constructor))__##name(void) {    \
	vlog_##name = vlog_register_type_and_pick_level(#name,	\
						    VLOG_##level);	            \
}




#ifdef __cplusplus
}
#endif


#endif /* _VLOG_H_ */