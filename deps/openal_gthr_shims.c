/**
 * Shims providing GCC win32 threading stubs required by libOpenAL32.a.
 *
 * libOpenAL32.a in kfx-deps was compiled with GCC's win32 threading model.
 * Our toolchain uses posix threading; the six __gthr_win32_* functions are not
 * provided by libgcc when using posix threads. This file provides them using
 * the Windows CRITICAL_SECTION API that the win32 threading model expects.
 */

#include <windows.h>

/* GCC win32 threading uses CRITICAL_SECTION for mutexes */
typedef CRITICAL_SECTION __gthread_win32_mutex_t;

int __gthr_win32_once(int *once, void (*func)(void))
{
    static CRITICAL_SECTION cs;
    static int cs_init = 0;
    if (!cs_init) {
        InitializeCriticalSection(&cs);
        cs_init = 1;
    }
    EnterCriticalSection(&cs);
    if (!*once) {
        *once = 1;
        LeaveCriticalSection(&cs);
        func();
    } else {
        LeaveCriticalSection(&cs);
    }
    return 0;
}

int __gthr_win32_mutex_init_function(__gthread_win32_mutex_t *mutex)
{
    InitializeCriticalSection(mutex);
    return 0;
}

int __gthr_win32_mutex_destroy(__gthread_win32_mutex_t *mutex)
{
    DeleteCriticalSection(mutex);
    return 0;
}

int __gthr_win32_mutex_lock(__gthread_win32_mutex_t *mutex)
{
    EnterCriticalSection(mutex);
    return 0;
}

int __gthr_win32_mutex_unlock(__gthread_win32_mutex_t *mutex)
{
    LeaveCriticalSection(mutex);
    return 0;
}

void __gthr_win32_yield(void)
{
    Sleep(0);
}
