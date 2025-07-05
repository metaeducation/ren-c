template<typename T>
struct MyWrapper {
    using wrapped_type = T;
    T value;
};

inline void test_needful_const()
{
    // Test fundamental types
    STATIC_ASSERT_SAME(needful_constify_type(int), int);
    STATIC_ASSERT_SAME(needful_unconstify_type(const int), int);

    // Test enum types
    enum MyEnum { A, B, C };
    STATIC_ASSERT_SAME(needful_constify_type(MyEnum), MyEnum);
    STATIC_ASSERT_SAME(needful_unconstify_type(const MyEnum), MyEnum);

    // Test regular class types
    struct MyClass {};
    STATIC_ASSERT_SAME(needful_constify_type(MyClass), MyClass);
    STATIC_ASSERT_SAME(needful_unconstify_type(const MyClass), MyClass);

    // Test pointer types
    STATIC_ASSERT_SAME(needful_constify_type(int*), const int*);
    STATIC_ASSERT_SAME(needful_unconstify_type(const int*), int*);

    // Test template wrapper types
    STATIC_ASSERT_SAME(
        needful_constify_type(MyWrapper<int>), MyWrapper<const int>
    );
    STATIC_ASSERT_SAME(
        needful_constify_type(MyWrapper<MyClass>), MyWrapper<const MyClass>
    );
    STATIC_ASSERT_SAME(
        needful_unconstify_type(MyWrapper<const MyClass>), MyWrapper<MyClass>
    );

    STATIC_ASSERT_SAME(
        needful_constify_type(MyWrapper<int*>), MyWrapper<const int*>
    );

    STATIC_ASSERT_SAME(
        needful_merge_const(MyWrapper<const int*>, char*), const char*
    );
    STATIC_ASSERT_SAME(
        needful_merge_const(MyWrapper<int*>, char*), char*
    );
    STATIC_ASSERT_SAME(
        needful_merge_const(MyWrapper<int*>, const char*), const char*
    );
    STATIC_ASSERT_SAME(
        needful_merge_const(MyWrapper<const int*>, const char*), const char*
    );

    STATIC_ASSERT_SAME(
        needful_merge_const(const int*, char*), const char*
    );
    STATIC_ASSERT_SAME(
        needful_merge_const(int*, char*), char*
    );
    STATIC_ASSERT_SAME(
        needful_merge_const(int*, const char*), const char*
    );
    STATIC_ASSERT_SAME(
        needful_merge_const(const int*, const char*), const char*
    );
}
