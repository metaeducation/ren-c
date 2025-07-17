#define STATIC_ASSERT_SAME(T1,T2) \
    static_assert(std::is_same<T1,T2>::value, "Types are not the same")

namespace needful {  //=//// BEGIN `needful::` NAMESPACE //////////////////=//

    namespace test_const {
        #include "test-needful-const.hpp"
    }

    namespace test_casts {
        #include "test-needful-casts.hpp"
    }

  #if NEEDFUL_OPTION_USES_WRAPPER
    namespace test_option {
        #include "test-needful-option.hpp"
    }
  #endif

} //=//// END `needful::` NAMESPACE ///////////////////////////////////////=//
