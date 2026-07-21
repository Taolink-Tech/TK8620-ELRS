#include <stddef.h>
#include "common.h"
#include "config.h"
#include "logging.h"
#include "tk86xx_platform.h"

#define MAX_DEFERRED_FUNCTIONS (3)

typedef struct {
    unsigned long started;
    unsigned long timeout;
    void (*function)(void);
} deferred_t;

static deferred_t deferred[MAX_DEFERRED_FUNCTIONS] = {
    {0, 0, NULL},
    {0, 0, NULL},
    {0, 0, NULL},
};

void deferExecutionMicros(unsigned long us, void (*function)(void))
{
    for (int i=0 ; i<MAX_DEFERRED_FUNCTIONS ; i++)
    {
        if (deferred[i].function == NULL) {
            deferred[i].started = micros();
            deferred[i].timeout = us;
            deferred[i].function = function;
            return;
        }
    }

    // Bail out, there are no slots available!
    DBGLN("No more deferred function slots available!");
}

void executeDeferredFunction(unsigned long now)
{
    // execute deferred function if its time has elapsed
    for (int i=0 ; i<MAX_DEFERRED_FUNCTIONS ; i++)
    {
        if (deferred[i].function != NULL && (now - deferred[i].started) > deferred[i].timeout)
        {
            deferred[i].function();
            deferred[i].function = NULL;
        }
    }
}
