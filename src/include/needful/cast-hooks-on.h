#if !defined(NEEDFUL_H)
    STATIC_FAIL(needful_h_must_be_included_before_cast_runtime_on_h)
#endif

#if CPLUSPLUS_11
    #undef h_cast
    #define h_cast  needful_lenient_hookable_cast
#endif
