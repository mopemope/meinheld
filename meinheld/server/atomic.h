

uint32_t
atomic_cas(volatile uint32_t *mem, uint32_t with, uint32_t cmp)
{
    uint32_t prev;

    __asm__ volatile ("lock; cmpxchgl %1, %2"
                  : "=a" (prev)
                  : "r" (with), "m" (*(mem)), "0"(cmp)
                  : "memory", "cc");
    return prev;
}

#define trylock(lock)  ((lock) == 1 && atomic_cas(&lock, 0, 1))
#define unlock(lock)    (lock) = 1
