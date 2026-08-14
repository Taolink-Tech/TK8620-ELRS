#include "unified_config.h"

#if ELRS_HAS_AIRPORT

#include <stdarg.h>
#include <stdint.h>

#include "tk86xx_api.h"

#define TK_PRINTF_BUFFER_SIZE 256U

extern int tiny_vsnprintf_like(char *buffer, int size, const char *format, va_list args);

static char s_tkPrintfBuffer[TK_PRINTF_BUFFER_SIZE];

void tk_printf(const char *format, ...)
{
    va_list args;
    int written;

    if (!UnifiedConfig_IsLoggingEnabled() || format == NULL) {
        return;
    }

    va_start(args, format);
    written = tiny_vsnprintf_like(s_tkPrintfBuffer, (int)sizeof(s_tkPrintfBuffer), format, args);
    va_end(args);

    if (written > 0) {
        uint32_t length = (uint32_t)written;
        if (length >= sizeof(s_tkPrintfBuffer)) {
            length = sizeof(s_tkPrintfBuffer) - 1U;
        }
        (void)Tk86xxSerialWrite((const UINT8 *)s_tkPrintfBuffer, length);
    }
}

#endif
