extern "C"{
#include <arch.h>
}

// This hack is needed because Beken SDK is not linking against libstdc++ correctly.
extern "C" {
void * __dso_handle = 0;
}

// Warkaround for ld error:undefined reference to '__sync_synchronize'
// refer to https://stackoverflow.com/questions/64658430/gnu-arm-embedded-toolchain-undefined-reference-to-sync-synchronize
extern "C" void __sync_synchronize() {}

extern "C" unsigned int __atomic_fetch_add_4(volatile void * ptr, unsigned int val, int memorder)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    unsigned int tmp = (*(unsigned int *) ptr + val);
    *(unsigned int *) ptr = tmp;
    GLOBAL_INT_RESTORE();
    return tmp;
}

extern "C" bool __atomic_compare_exchange_4(volatile void * pulDestination, void * ulComparand, unsigned int desired, bool weak,
                                            int success_memorder, int failure_memorder)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    bool ulReturnValue;
    if (*(unsigned int *) pulDestination == *(unsigned int *) ulComparand)
    {
        *(unsigned int *) pulDestination = desired;
        ulReturnValue                    = true;
    }
    else
    {
        *(unsigned int *) ulComparand = *(unsigned int *) pulDestination;
        ulReturnValue                 = false;
    }
    GLOBAL_INT_RESTORE();
    return ulReturnValue;
}

extern "C" unsigned int __atomic_fetch_sub_4(volatile void * ptr, unsigned int val, int memorder)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    unsigned int tmp = (*(unsigned int *) ptr - val);
    *(unsigned int *) ptr = tmp;
    GLOBAL_INT_RESTORE();
    return tmp;
}
extern "C" bool __atomic_compare_exchange_1(volatile void * pulDestination, void * ulComparand, unsigned char desired, bool weak,
                                            int success_memorder, int failure_memorder)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    bool ulReturnValue;
    if (*(unsigned char *) pulDestination == *(unsigned char *) ulComparand)
    {
        *(unsigned char *) pulDestination = desired;
        ulReturnValue                     = true;
    }
    else
    {
        *(unsigned char *) ulComparand = *(unsigned char *) pulDestination;
        ulReturnValue                  = false;
    }
    GLOBAL_INT_RESTORE();
    return ulReturnValue;
}

extern "C" unsigned int __atomic_fetch_and_4(volatile void * pulDestination, unsigned int ulValue, int memorder)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    unsigned int ulCurrent;

    ulCurrent = *(unsigned int *) pulDestination;
    *(unsigned int *) pulDestination &= ulValue;
    GLOBAL_INT_RESTORE();
    return ulCurrent;
}

extern "C" bool __sync_bool_compare_and_swap_4(volatile void * ptr, unsigned int oldval, unsigned int newval)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    if (*(unsigned int *) ptr == oldval)
    {
        *(unsigned int *) ptr = newval;
        GLOBAL_INT_RESTORE();
        return true;
    }
    else
    {
        GLOBAL_INT_RESTORE();
        return false;
    }
}

extern "C" bool __sync_bool_compare_and_swap_1(volatile void * ptr, unsigned char oldval, unsigned char newval)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    if (*(unsigned char *) ptr == oldval)
    {
        *(unsigned char *) ptr = newval;
        GLOBAL_INT_RESTORE();
        return true;
    }
    else
    {
        GLOBAL_INT_RESTORE();
        return false;
    }
}

/* stub for __libc_init_array */
extern "C" void _fini(void) {}
extern "C" void _init(void)
{
    ;
}
