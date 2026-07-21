#pragma once
#include <stdint.h>
#include <string.h>
#include <stddef.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define UNUSED(x) (void)(x)
#define WORD_ALIGNED_ATTR __attribute__((aligned(4)))

/*
 * Keep these helpers simple and portable for the tk8620 build.
 * The current implementation intentionally uses plain ternary macros.
 */
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))

inline const char *strchrnul(const char *pos, const char find)
{
    const char *semi = strchr(pos, find);
    if (semi == NULL)
    {
        semi = pos + strlen(pos);
    }
    return semi;
}
