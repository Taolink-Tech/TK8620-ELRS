#ifndef DEBUG_H
#define DEBUG_H

#include "tk_printf.h"
#include "unified_config.h"
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

#if ELRS_AIRPORT
  /* Dedicated AirPort images omit UART diagnostics so transparent data cannot
     be contaminated. */
  #define DBGCR        do { if (0) tk_printf("\r\n"); } while (0)
  #define DBGW(c)      do { if (0) tk_printf("%c", (c)); } while (0)
  #define DBG(msg, ...) do { if (0) tk_printf(msg, ##__VA_ARGS__); } while (0)
  #define DBGLN(msg, ...) do { if (0) tk_printf(msg, ##__VA_ARGS__); } while (0)
  #define ERRLN(msg, ...) do { if (0) tk_printf(msg, ##__VA_ARGS__); } while (0)
  #define DBGVCR
  #define DBGV(...)
  #define DBGVLN(...)
#elif ELRS_UNIFIED
  /* tk_printf() is the single runtime gate for both application and SDK logs. */
  #define DBGCR        tk_printf("\r\n")
  #define DBGW(c)      tk_printf("%c", (c))
  #define DBG(msg, ...) tk_printf(msg, ##__VA_ARGS__)
  #define DBGLN(msg, ...) do { \
    tk_printf(msg, ##__VA_ARGS__); \
    tk_printf("\r\n"); \
  } while (0)
  #define ERRLN(msg, ...) do { \
    tk_printf("ERROR: "); \
    tk_printf(msg, ##__VA_ARGS__); \
    tk_printf("\r\n"); \
  } while (0)
  #define DBGVCR
  #define DBGV(...)
  #define DBGVLN(...)
#elif defined(DEBUG_LOG) && !defined(CRITICAL_FLASH)
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
  #define ERRLN(...)
#endif

#endif
