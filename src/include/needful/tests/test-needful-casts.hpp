template<typename T>
struct MyWrapper {
    NEEDFUL_DECLARE_WRAPPED_FIELD (T, value);
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

#define SINGLE_ARG_MACRO(x) NEEDFUL_USED(x)
#define OUTER_MACRO(arg) SINGLE_ARG_MACRO(arg)

// In the C++ standard:
//
// "The number of arguments in an invocation of a function-like macro shall
//  agree with the number of parameters in the macro definition, and each
//  argument is delimited by commas outside parentheses, brackets, and braces."
//
// Hence angle-brackets are not considered delimiters for macro arguments
// (although MSVC is known to treat them as such, anyway).
//
// So if a macro expands to C++ code with commas inside angle brackets, the
// macro should wrap that in parentheses.  We catch violations here.
//
inline void test_cast_macro_commas()
{
    OUTER_MACRO(needful_xtreme_cast(int, 0));

    OUTER_MACRO(needful_lenient_hookable_cast(int, 0));
    OUTER_MACRO(needful_lenient_unhookable_cast(int, 0));

    OUTER_MACRO(needful_rigid_hookable_cast(int, 0));
    OUTER_MACRO(needful_rigid_unhookable_cast(int, 0));

    OUTER_MACRO(needful_upcast(int, 0));

    OUTER_MACRO(needful_hookable_downcast nullptr);
    OUTER_MACRO(needful_unhookable_downcast nullptr);

    intptr_t i = 0;
    int* ip = nullptr;
    void(*fp)() = nullptr;
    void *vp = nullptr;

    OUTER_MACRO(needful_pointer_cast(int*, i));
    OUTER_MACRO(needful_integral_cast(intptr_t, ip));
    OUTER_MACRO(needful_function_cast(void(*)(int), fp));
    OUTER_MACRO(needful_valist_cast(va_list*, vp));

    const int* cip = nullptr;
    OUTER_MACRO(needful_mutable_cast(int*, cip));
}

#undef SINGLE_ARG_MACRO
#undef OUTER_MACRO


inline void test_mutability_cast()
{
    struct Base {};
    struct Derived : Base {};

    const Derived* cd = nullptr;
    Base* b = needful_mutable_cast(Base*, cd);
    UNUSED(b);

    /*  // this should not work, test suite should make sure of that
    const Base* cb = nullptr;
    Derived* d = needful_mutable_cast(Derived*, cb);
    UNUSED(d);
    */
}
