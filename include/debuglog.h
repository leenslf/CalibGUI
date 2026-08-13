#ifndef CALIBGUI_DEBUGLOG_H
#define CALIBGUI_DEBUGLOG_H


#include "config.h"


#if defined(DEBUG_ALL) || (defined(DEBUG_LOCAL) && !defined(DEBUG_DISABLE))
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#define LOGPRINTF(...) printf(__VA_ARGS__)
#define ERRPRINTF(...) fprintf(stderr, __VA_ARGS__)

#define DBGPRINT(...) PRINTF(__VA_ARGS__)
#define ERRPRINT(...) ERRPRINTF(__VA_ARGS__)

#endif
