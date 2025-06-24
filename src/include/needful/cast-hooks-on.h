#if (! NEEDFUL_CASTS_H)
    STATIC_FAIL(needful_casts_h_must_be_included_before_cast_runtime_on_h)
#endif

#if CPLUSPLUS_11
    #undef d_cast
    #define d_cast  Validated_Downcast

    #undef h_cast
    #define h_cast  Validated_Cast

    #undef c_cast
    #define c_cast  Validated_Const_Preserving_Cast
#endif
