// Memory limit and JIT for TrollStore / TrollStore Lite / Dopamine installs (App/TrollStore.entitlements).
// Everything is looked up at run time and every failure is logged and ignored: a build without these entitlements
// (AltStore, SideStore) runs the same code and simply gets "not available" for each step.
#include "NativePrivileged.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

// xnu's <sys/kern_memorystatus.h> is not in the iPhone SDK.
enum { PG_MEMORYSTATUS_CMD_SET_JETSAM_TASK_LIMIT = 6 };
// <sys/codesign.h> is not in the iPhone SDK either.
enum { PG_CS_OPS_STATUS = 0 };
enum { PG_CS_DEBUGGED = 0x10000000 };
enum { PG_PT_TRACE_ME = 0 };
extern int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);

static char summary[512] = "privileged boot: not run";
static void note(const char *format, ...) __attribute__((format(printf, 1, 2)));
static size_t used;
static void note(const char *format, ...) {
  __builtin_va_list args; __builtin_va_start(args, format);
  if (used < sizeof summary - 1) used += (size_t)vsnprintf(summary + used, sizeof summary - used, format, args);
  __builtin_va_end(args);
  if (used >= sizeof summary) used = sizeof summary - 1;
}

bool pg_jit_available(void) {
  uint32_t flags = 0;
  if (csops(getpid(), PG_CS_OPS_STATUS, &flags, sizeof flags) != 0) return false;
  return (flags & PG_CS_DEBUGGED) != 0;
}

// Raises this process's own Jetsam limit. The game plans its streaming from os_proc_available_memory(), which is
// (limit - footprint), so this is what lets it keep more textures and models instead of evicting and reloading them.
static void raise_memory_limit(int percent) {
  typedef int (*Control)(uint32_t, int32_t, uint32_t, void *, size_t);
  Control control = (Control)dlsym(RTLD_DEFAULT, "memorystatus_control");
  if (!control) { note("memory: memorystatus_control missing; "); return; }
  uint64_t ram = 0; size_t size = sizeof ram;
  if (sysctlbyname("hw.memsize", &ram, &size, NULL, 0) != 0 || !ram) { note("memory: RAM unknown; "); return; }
  // The system and the jailbreak's own daemons need room too: a 3 GB phone (iPhone X, 8) gets 70% (about 2.1 GB, against
  // about 1.85 GB without this), 4 GB and up get 80%.
  if (percent < 50 || percent > 90) percent = ram < (3500ull << 20) ? 70 : 80;
  uint64_t limitMB = ram / (1024 * 1024) / 100 * (uint64_t)percent;
  // For this command the limit in MB goes in the 'flags' argument; the pid is our own.
  int result = control(PG_MEMORYSTATUS_CMD_SET_JETSAM_TASK_LIMIT, getpid(), (uint32_t)limitMB, NULL, 0);
  if (result == 0) note("memory: limit set to %llu MB (%d%% of RAM); ", (unsigned long long)limitMB, percent);
  else note("memory: not allowed (errno %d: needs the com.apple.private.memorystatus entitlement, i.e. TrollStore or a jailbreak); ", errno);
}

static void enable_jit(void) {
  if (pg_jit_available()) { note("jit: already on; "); return; }
  // Dopamine: its libjailbreak can mark this process as debugged, which is what allows executable memory.
  static const char *const paths[] = {"/var/jb/basebin/libjailbreak.dylib", "/private/var/jb/basebin/libjailbreak.dylib"};
  for (unsigned i = 0; i < sizeof paths / sizeof *paths && !pg_jit_available(); i++) {
    void *handle = dlopen(paths[i], RTLD_NOW);
    if (!handle) continue;
    typedef int (*SetDebugged)(uint64_t pid, bool fullyDebugged);
    SetDebugged set = (SetDebugged)dlsym(handle, "jbclient_platform_set_process_debugged");
    if (set) { int r = set((uint64_t)getpid(), false); note("jit: Dopamine call returned %d; ", r); }
  }
  // TrollStore without a jailbreak: with get-task-allow a process can trace itself, which sets the same flag. A traced
  // process stops on every signal until a tracer continues it, and the engine uses signals, so this is OFF unless the
  // environment asks for it (PG_JIT_TRACE=1).
  const char *trace = getenv("PG_JIT_TRACE");
  if (!pg_jit_available() && trace && trace[0] == '1') {
    typedef int (*Ptrace)(int, int, void *, int);
    Ptrace ptraceCall = (Ptrace)dlsym(RTLD_DEFAULT, "ptrace");
    if (ptraceCall) { int r = ptraceCall(PG_PT_TRACE_ME, 0, NULL, 0); note("jit: PT_TRACE_ME returned %d; ", r); }
  }
  note(pg_jit_available() ? "jit: ON; " : "jit: not available (the emulator for .asi mods stays on its interpreter); ");
}

void pg_privileged_boot(bool enableJIT, int limitPercent) {
  static bool done; if (done) return; done = true;
  summary[0] = 0; used = 0;
  raise_memory_limit(limitPercent);
  if (enableJIT) enable_jit(); else note("jit: off; ");
}

const char *pg_privileged_summary(void) { return summary; }
