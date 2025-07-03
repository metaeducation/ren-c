#if !defined(NEEDFUL_H)
    STATIC_FAIL(needful_h_must_be_included_before_cast_hooks_off_h)
#endif

#if CPLUSPLUS_11
    #undef h_cast
    #define h_cast  Unhookable_Cast

    #undef c_cast
    #define c_cast  Unhookable_Const_Preserving_Cast
#endif
