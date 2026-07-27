#ifndef THREAD_COMPAT_H
#define THREAD_COMPAT_H

#ifdef _WIN32
    #ifndef WINVER
        #define WINVER 0x0600
    #endif
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #include <winsock2.h>
    #include <windows.h>
    #include <process.h>
    #include <stdlib.h>
    #include <stdint.h>

    typedef HANDLE pthread_t;
    typedef CRITICAL_SECTION pthread_mutex_t;
    typedef HANDLE pthread_cond_t;

    #define PTHREAD_MUTEX_INITIALIZER {0}

    static inline int pthread_mutex_init(pthread_mutex_t *m, void *attr) {
        (void)attr;
        InitializeCriticalSection(m);
        return 0;
    }
    static inline int pthread_mutex_lock(pthread_mutex_t *m) {
        EnterCriticalSection(m);
        return 0;
    }
    static inline int pthread_mutex_unlock(pthread_mutex_t *m) {
        LeaveCriticalSection(m);
        return 0;
    }
    static inline int pthread_mutex_destroy(pthread_mutex_t *m) {
        DeleteCriticalSection(m);
        return 0;
    }

    static inline int pthread_cond_init(pthread_cond_t *c, void *attr) {
        (void)attr;
        *c = CreateEvent(NULL, FALSE, FALSE, NULL);
        return 0;
    }
    static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
        LeaveCriticalSection(m);
        WaitForSingleObject(*c, INFINITE);
        EnterCriticalSection(m);
        return 0;
    }
    static inline int pthread_cond_signal(pthread_cond_t *c) {
        SetEvent(*c);
        return 0;
    }
    static inline int pthread_cond_broadcast(pthread_cond_t *c) {
        SetEvent(*c);
        return 0;
    }
    static inline int pthread_cond_destroy(pthread_cond_t *c) {
        CloseHandle(*c);
        return 0;
    }

    typedef void *(*thread_func_t)(void *);
    typedef struct {
        thread_func_t func;
        void *arg;
    } WinThreadArgs;

    static inline unsigned __stdcall win_thread_adapter(void *p) {
        WinThreadArgs *args = (WinThreadArgs *)p;
        thread_func_t f = args->func;
        void *a = args->arg;
        free(args);
        f(a);
        return 0;
    }

    static inline int pthread_create(pthread_t *thread, void *attr, thread_func_t start_routine, void *arg) {
        (void)attr;
        WinThreadArgs *args = (WinThreadArgs *)malloc(sizeof(WinThreadArgs));
        if (!args) return -1;
        args->func = start_routine;
        args->arg = arg;
        uintptr_t handle = _beginthreadex(NULL, 0, win_thread_adapter, args, 0, NULL);
        if (handle == 0) {
            free(args);
            return -1;
        }
        *thread = (HANDLE)handle;
        return 0;
    }

    static inline int pthread_detach(pthread_t thread) {
        CloseHandle(thread);
        return 0;
    }
#else
    #include <pthread.h>
#endif

#endif /* THREAD_COMPAT_H */