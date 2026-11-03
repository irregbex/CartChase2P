#ifndef DKC1_WINDOWS_COMPAT_H
#define DKC1_WINDOWS_COMPAT_H
#include <windows.h>
#include <direct.h>
#include <stdlib.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define mkdir(path, mode) _mkdir(path)
#define chdir _chdir
#define realpath(path, result) _fullpath(result, path, PATH_MAX)
static int setenv(const char *name, const char *value, int overwrite) {
  return !overwrite && getenv(name) ? 0 : _putenv_s(name, value);
}
static uint64_t mach_absolute_time(void) {
  LARGE_INTEGER time; QueryPerformanceCounter(&time); return (uint64_t)time.QuadPart;
}
typedef struct { uint32_t numer, denom; } mach_timebase_info_data_t;
static void mach_timebase_info(mach_timebase_info_data_t *info) {
  LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
  info->numer=1000000000; info->denom=(uint32_t)frequency.QuadPart;
}
static int mach_wait_until(uint64_t deadline) {
  LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
  int64_t remaining=(int64_t)deadline-(int64_t)mach_absolute_time();
  if (remaining>0) {
    /* High-resolution one-shot wait, not a second presentation cadence. */
    HANDLE timer=CreateWaitableTimerExW(NULL,NULL,2,TIMER_ALL_ACCESS);
    if (timer) {
      LARGE_INTEGER due; due.QuadPart=-(remaining*10000000/frequency.QuadPart);
      if (SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE)) WaitForSingleObject(timer,INFINITE);
      CloseHandle(timer);
    } else Sleep((DWORD)(remaining*1000/frequency.QuadPart));
  }
  return 0;
}
#endif
