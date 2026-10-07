/*
        The fold above, taken away where FORTIFY_SOURCE is on.

        include/linux/fortify-string.h defines memcpy and memset itself, after
        asm/string_64.h has been read and with no #undef in front, so every
        translation unit that includes linux/string.h -- some twenty thousand
        of them -- meets a second definition of each and says so: a pair of
        "redefined" warnings an object, which is millions of lines in a build
        and enough to hide the one that matters. The fortify wrappers are the
        ones that win, and they end in __builtin_memcpy and __builtin_memset,
        which is what the fold made of the sizes it folded, so nothing is lost
        by leaving them the names.
*/
#ifndef MOONWATER_FOLD_X86_FORTIFY
#define MOONWATER_FOLD_X86_FORTIFY 1
#ifdef CONFIG_FORTIFY_SOURCE
#undef memcpy
#undef memset
#endif
#endif /* MOONWATER_FOLD_X86_FORTIFY */
