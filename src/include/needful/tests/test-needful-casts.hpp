template<typename T>
struct MyWrapper {
    using wrapped_type = T;
    T value;
};

#undef Needful_Hookable_Cast
#define Needful_Hookable_Cast(T,expr) /* outer parens [C] */ \
    (needful::Hookable_Cast_Helper<T>(expr))  // func: universal refs [3]


inline void test_needful_const()
{
    char data[] = "some data";
    char* mdata = data;
    const char* cdata = data;

    STATIC_ASSERT_SAME(
        decltype(Needful_Mutable_Cast(char*, cdata)),  // cast away const
        char*  // const should be gone
    );

    STATIC_ASSERT_SAME(
        decltype(Needful_Mutable_Cast(const char*, cdata)),  // cast to const
        const char*  // mutable casts can keep const
    );

    STATIC_ASSERT_SAME(
        decltype(Needful_Mutable_Cast(const char*, mdata)),  // cast to const
        const char*  // mutable casts can add const at the moment
    );

    STATIC_ASSERT_SAME(
        decltype(Needful_Hookable_Cast(char*, cdata)),
        const char*
    );

    STATIC_ASSERT_SAME(
        decltype(Needful_Hookable_Cast(char*, mdata)),
        char*
    );
}
