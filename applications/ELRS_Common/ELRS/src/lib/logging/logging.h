#ifndef DEBUG_H
#define DEBUG_H

#include "tk_printf.h"
#define DEBUG_LOG

// The public TK8620 build always routes logs through tk_printf().
#if defined(DEBUG_LOG)
#define DEBUG_ENABLED
#endif

#if defined(LOG_INIT)
void debugCreateInitLogger();
void debugFreeInitLogger();
#else
#define debugCreateInitLogger()
#define debugFreeInitLogger()
#endif

#if defined(CRITICAL_FLASH) || ((defined(DEBUG_RCVR_LINKSTATS)) && !defined(DEBUG_LOG))
  #define ERRLN(msg, ...)
#endif

#if defined(DEBUG_LOG) && !defined(CRITICAL_FLASH)
  #define DBGCR        tk_printf("\r\n")
  #define DBGW(c)      tk_printf("%c", (c))
  #define DBG(msg, ...)   tk_printf(msg, ##__VA_ARGS__)
  #define DBGLN(msg, ...) do { \
    tk_printf(msg, ##__VA_ARGS__); \
    tk_printf("\r\n"); \
  } while(0)
  #define ERRLN(msg, ...) do { \
    tk_printf("ERROR: "); \
    tk_printf(msg, ##__VA_ARGS__); \
    tk_printf("\r\n"); \
  } while(0)
  #define DBGVCR
  #define DBGV(...)
  #define DBGVLN(...)
#else
  #define DBGCR
  #define DBGW(c)
  #define DBG(...)
  #define DBGLN(...)
  #define DBGVCR
  #define DBGV(...)
  #define DBGVLN(...)
#endif

#endif
