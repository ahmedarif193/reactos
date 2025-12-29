typedef struct pthread_mutex_t pthread_mutex_t;
typedef struct pthread_mutexattr_t pthread_mutexattr_t;

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    (void)mutex;
    (void)attr;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}
