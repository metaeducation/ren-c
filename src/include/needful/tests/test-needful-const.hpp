template<typename T>
struct MyWrapper {
    NEEDFUL_DECLARE_WRAPPED_FIELD (T, value);
};

inline void test_needful_const()
{
    STATIC_ASSERT(std::is_same<int, int>::value);  // test embedded <,>

    // Test fundamental types
    STATIC_ASSERT_SAME(needful_constify_t(int), int);
    STATIC_ASSERT_SAME(needful_unconstify_t(const int), int);

    // Test enum types
    enum MyEnum { A, B, C };
    STATIC_ASSERT_SAME(needful_constify_t(MyEnum), MyEnum);
    STATIC_ASSERT_SAME(needful_unconstify_t(const MyEnum), MyEnum);

    // Test regular class types
    struct MyClass {};
    STATIC_ASSERT_SAME(needful_constify_t(MyClass), MyClass);
    STATIC_ASSERT_SAME(needful_unconstify_t(const MyClass), MyClass);

    // Test pointer types
    STATIC_ASSERT_SAME(needful_constify_t(int*), const int*);
    STATIC_ASSERT_SAME(needful_unconstify_t(const int*), int*);

    // Test template wrapper types
    STATIC_ASSERT_SAME(
        needful_constify_t(MyWrapper<int>), MyWrapper<const int>
    );
    STATIC_ASSERT_SAME(
        needful_constify_t(MyWrapper<MyClass>), MyWrapper<const MyClass>
    );
    STATIC_ASSERT_SAME(
        needful_unconstify_t(MyWrapper<const MyClass>), MyWrapper<MyClass>
    );

    STATIC_ASSERT_SAME(
        needful_constify_t(MyWrapper<int*>), MyWrapper<const int*>
    );

    STATIC_ASSERT_SAME(
        needful_merge_const_t(MyWrapper<const int*>, char*), const char*
    );
    STATIC_ASSERT_SAME(
        needful_merge_const_t(MyWrapper<int*>, char*), char*
    );
    STATIC_ASSERT_SAME(
        needful_merge_const_t(MyWrapper<int*>, const char*), const char*
    );
    STATIC_ASSERT_SAME(
        needful_merge_const_t(MyWrapper<const int*>, const char*), const char*
    );

    STATIC_ASSERT_SAME(
        needful_merge_const_t(const int*, char*), const char*
    );
    STATIC_ASSERT_SAME(
        needful_merge_const_t(int*, char*), char*
    );
    STATIC_ASSERT_SAME(
        needful_merge_const_t(int*, const char*), const char*
    );
    STATIC_ASSERT_SAME(
        needful_merge_const_t(const int*, const char*), const char*
    );
}
