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
    STATIC_ASSERT_SAME(needful_constify_type(MyClass), const MyClass);
    STATIC_ASSERT_SAME(needful_unconstify_type(const MyClass), MyClass);

    // Test pointer types
    STATIC_ASSERT_SAME(needful_constify_type(int*), const int*);
    STATIC_ASSERT_SAME(needful_unconstify_type(const int*), int*);

    // Test template wrapper types
    STATIC_ASSERT_SAME(
        needful_constify_type(MyWrapper<int>), MyWrapper<int>
    );
    STATIC_ASSERT_SAME(
        needful_constify_type(MyWrapper<MyClass>), MyWrapper<const MyClass>
    );
    STATIC_ASSERT_SAME(
        needful_unconstify_type(MyWrapper<const MyClass>), MyWrapper<MyClass>
    );
}
