template<typename T>
struct MyWrapper {
    using wrapped_type = T;
    T value;
};


inline void test_needful_const()
{
    char data[] = "some data";
    char* mdata = data;
    const char* cdata = data;

    constexpr int trivial = needful_lenient_hookable_cast(int, 'a');
    NEEDFUL_UNUSED(trivial);

    constexpr int u_trivial = needful_lenient_unhookable_cast(int, 'a');
    NEEDFUL_UNUSED(u_trivial);

    STATIC_ASSERT_SAME(
        decltype(needful_mutable_cast(char*, cdata)),  // cast away const
        char*  // const should be gone
    );

    STATIC_ASSERT_SAME(
        decltype(needful_mutable_cast(const char*, cdata)),  // cast to const
        const char*  // mutable casts can keep const
    );

    STATIC_ASSERT_SAME(
        decltype(needful_mutable_cast(const char*, mdata)),  // cast to const
        const char*  // mutable casts can add const at the moment
    );

    STATIC_ASSERT_SAME(
        decltype(needful_lenient_hookable_cast(char*, cdata)),
        const char*
    );

    STATIC_ASSERT_SAME(
        decltype(needful_lenient_hookable_cast(char*, mdata)),
        char*
    );
}
