#pragma once
#include <stdbool.h>
#include <stdint.h>
// Extra powers for installs that have them (TrollStore, TrollStore Lite, Dopamine). On a normal sideload every call
// below fails quietly and the app behaves exactly as before.
// Call once at launch, before anything reads the memory limit. limitPercent: the share of the phone's RAM the app may
// use (0 = 80). enableJIT: also try to turn on JIT for this process.
void pg_privileged_boot(bool enableJIT, int limitPercent);
// One line for the log: what worked and what didn't.
const char *pg_privileged_summary(void);
// Whether this process may map executable memory now (CS_DEBUGGED).
bool pg_jit_available(void);
