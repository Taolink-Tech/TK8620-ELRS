#pragma once


void deferExecutionMicros(unsigned long us, void (*function)(void));
void executeDeferredFunction(unsigned long now);

static inline void deferExecutionMillis(unsigned long ms, void (*function)(void))
{
    deferExecutionMicros(ms * 1000, function);
}