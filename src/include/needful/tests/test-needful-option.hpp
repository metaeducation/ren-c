template<typename T>
struct MyWrapper {
    NEEDFUL_DECLARE_WRAPPED_FIELD(T, value);

    MyWrapper() = default;

    MyWrapper(const T& init) : value {init} {}

    MyWrapper(NoneStruct&&) : value {0} {}  // Initialize to zero for "none"

    MyWrapper(Result0Struct&&) : value {0} {}  // need overload

    explicit operator bool() const {
        return value != 0;  // T needs a zero value representing "none"
    }
};

inline void test_needful_option()
{
    // Test with fundamental types
    Option(int) oi1 = 42;
    Option(int) oi2 = none;
    assert(not oi2); // Should be falsey
    assert(oi1);  // Should be truthy

    // int direct = oi1;  /* illegal */
    STATIC_ASSERT(not needful_is_convertible_v(decltype(oi1), int));

    STATIC_ASSERT_SAME(decltype(unwrap(oi1)), int);
    STATIC_ASSERT_SAME(decltype(opt(oi1)), int);

    int v1 = unwrap oi1; // Should succeed
    assert(v1 == 42);
    int v2 = opt oi2; // Should be nullptr or equivalent
    assert(v2 == 0);

    // Test with enum types
    enum MyEnum { A, B, C };
    Option(MyEnum) oe1 = A;
    Option(MyEnum) oe2 = none;
    assert(oe1);
    assert(not oe2);

    // MyEnum direct = oe1;  /* illegal */
    STATIC_ASSERT((not needful_is_convertible_v(decltype(oe1), MyEnum)));

    STATIC_ASSERT_SAME(decltype(unwrap oe1), MyEnum);
    STATIC_ASSERT_SAME(decltype(opt oe1), MyEnum);

    // Test with pointer types
    Option(const char*) op1 = "abc";
    Option(const char*) op2 = none;
    Option(char*) op3 = nullptr;
    assert(op1);
    assert(not op2);
    assert(not op3);

    // char* direct = op1;  /* illegal */
    STATIC_ASSERT((not needful_is_convertible_v(decltype(op1), const char*)));

    STATIC_ASSERT_SAME(decltype(unwrap op1), const char*);
    STATIC_ASSERT_SAME(decltype(opt op1), const char*);

    // Test with template wrapper types
    Option(MyWrapper<int>) ow1 = MyWrapper<int>{456};
    Option(MyWrapper<int>) ow2 = none;
    assert(ow1);
    assert(not ow2);

    // Test assignment and copy
    Option(int) oi3 = oi1;
    assert(oi3);
}
