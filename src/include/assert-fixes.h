#if RUNTIME_CHECKS && defined(__GLIBC__) && defined(__GLIBC_MINOR__) && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 23))

// fix bug https://sourceware.org/bugzilla/show_bug.cgi?id=18604
#undef assert
# if !defined __GNUC__ || defined __STRICT_ANSI__
#  define assert(expr) \
    ((expr) \
     ? __ASSERT_VOID_CAST (0) \
     : __assert_panic (#expr, __FILE__, __LINE__, __ASSERT_FUNCTION))
# else
#  define assert(expr) \
    ({ \
      if (expr) \
        ; /* empty */ \
      else \
        __assert_panic (#expr, __FILE__, __LINE__, __ASSERT_FUNCTION); \
    })
# endif

#endif //__GLIBC__
