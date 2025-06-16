//=//// ENABLE IF FOR SAME TYPE ///////////////////////////////////////////=//
//
// This is useful for SFINAE (Substitution Failure Is Not An Error), as a
// very common pattern.  It's variadic, so you can use it like:
//
//   template <typename T, EnableIfSame<T, TypeOne, TypeTwo> = nullptr>
//   INLINE bool operator==(const TypeThree& three, T&& t) = delete;
//
// Written out long form that would look like:
//
//    template <
//        typename T,
//        typename std::enable_if<
//            std::is_same<T, TypeOne>::value
//            or std::is_same<T, TypeTwo>::value
//        >::type* = nullptr
//     >
//     INLINE bool operator==(const TypeThree& three, T&& t) = delete;
//
#if CPLUSPLUS_11
    template <typename T, typename... Allowed>
    struct IsSameAny;

    template <typename T, typename First, typename... Rest>
    struct IsSameAny<T, First, Rest...> {
        static constexpr bool value =
            std::is_same<T, First>::value or IsSameAny<T, Rest...>::value;
    };

    template <typename T>
    struct IsSameAny<T> {
        static constexpr bool value = false;
    };

    template <typename T, typename... Allowed>
    using EnableIfSame =
        typename std::enable_if<IsSameAny<T, Allowed...>::value>::type*;
#endif


//=//// TYPE LIST HELPER //////////////////////////////////////////////////=//
//
// Type lists allow checking if a type is in a list of types at compile time:
//
//     template<typename T>
//     void process(T value) {
//         using NumericTypes = CTypeList<int, float, double>;
//         static_assert(NumericTypes::contains<T>{}, "T must be numeric");
//         // ...
//     }
//
// 1. For C++11 compatibility, it must be `List::contains<T>{}` with braces.
//    C++14 or higher has variable templates:
//
//        struct contains_impl {  /* instead of calling this `contains` */
//            enum { value = false };
//        };
//        template<typename T>
//        static constexpr bool contains = contains_impl<T>::value;
//
//    Without that capability, best we can do is to construct an instance via
//    a default constructor, and then have a constexpr implicit boolean
//    coercion for that instance.
//
// 2. To expose the type list functionality in a way that will look less
//    intimidating to C programmers, `DECLARE_C_TYPE_LIST` is provided, along
//    with `In_C_Type_List`.  This lets you make it look like a regular
//    function call:
//
//     template<typename T>
//     void process(T value) {
//         DECLARE_C_TYPE_LIST(numeric_types,
//             int, float, double
//         );
//         STATIC_ASSERT(In_C_Type_List(numeric_types, T));
//         // ...
//     }
//
//    While obfuscating the C++ is questionable in terms of people's education
//    about the language--and making compile-time code look like runtime
//    functions is a bit of a lie--this does make it easier.  It also means
//    the In_C_Type_List() function can be used in STATIC_ASSERT() macros
//    without making you write STATIC_ASSERT(()) to wrap < and > characters
//    that would appear with ::contains<T> at the callsite.
//
#if CPLUSPLUS_11
    template<typename... Ts>
    struct CTypeList {
        template<typename T>
        struct contains {
            enum { value = false };

            // Allow usage without ::value in most contexts [1]
            constexpr operator bool() const { return value; }
        };
    };

    template<typename T1, typename... Ts>
    struct CTypeList<T1, Ts...> {  // Specialization for non-empty lists
        template<typename T>
        struct contains {
            enum { value = std::is_same<T, T1>::value or
                        typename CTypeList<Ts...>::template contains<T>() };

            // Allow usage without ::value in most contexts [1]
            constexpr operator bool() const { return value; }
        };
    };

    #define DECLARE_C_TYPE_LIST(name, ...)  /* friendly for C [2] */ \
        using name = CTypeList<__VA_ARGS__>

    #define In_C_Type_List(list,T)  /* friendly for C [2] */ \
        list::contains<T>()
#endif


//=//// C FUNCTION TYPE (__cdecl) /////////////////////////////////////////=//
//
// Note that you *CANNOT* cast something like a `void *` to (or from) a
// function pointer.  Pointers to functions are not guaranteed to be the same
// size as to data, in either C or C++.  A compiler might count the number of
// functions in your program, find less than 255, and use bytes for function
// pointers:
//
//   http://stackoverflow.com/questions/3941793/
//
// So if you want something to hold either a function pointer or a data
// pointer, you have to implement that as a union...and know what you're doing
// when writing and reading it.
//
// 1. __cdecl is a Microsoft-specific thing that probably isn't really needed
//   in modern compilers, that presume cdecl.  Consider just dropping it:
//
//     http://stackoverflow.com/questions/3404372/
//
//
#if defined(_WIN32)  // 32-bit or 64-bit windows
    typedef void (__cdecl CFunction)(void);  // __cdecl is kind of outdated [1]
#else
    typedef void (CFunction)(void);
#endif

#define Apply_Cfunc(cfunc, ...)  /* make calls clearer at callsites */ \
    (*(cfunc))(__VA_ARGS__)


//=//// PREPROCESSOR ARGUMENT COUNT (w/MSVC COMPATIBILITY TWEAK) //////////=//
//
// While the external C API can be compiled with a C89 compiler (if you're
// willing to put rebEND at the tail of every variadic call), the core has
// committed to variadic macros.
//
// It can be useful to know the count of a __VA_ARGS__ list.  There are some
// techniques floating around that should work in MSVC, but do not.  This
// appears to work in MSVC.
//
// https://stackoverflow.com/a/5530998
//
// You can use this to implement optional parameters, e.g. the following will
// invoke F_1(), F_2(), F_3() etc. based on how many parameters it receives:
//
//    #define F(...) PP_CONCAT(SOMETHING_, PP_NARGS(__VA_ARGS__))(__VA_ARGS__)

#define PP_EXPAND(x) x  // required for MSVC in optional args, see link above

#define PP_CONCAT_IMPL(A, B) A##B
#define PP_CONCAT(A, B) PP_CONCAT_IMPL(A, B)

#define PP_NARGS_IMPL(x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,N,...) N  // 0 won't work
#define PP_NARGS(...) \
    PP_EXPAND(PP_NARGS_IMPL(__VA_ARGS__,10,9,8,7,6,5,4,3,2,1,0))


//=//// TESTING IF A NUMBER IS FINITE /////////////////////////////////////=//
//
// C89 and C++98 had no standard way of testing for if a number was finite or
// not.  Windows and POSIX came up with their own methods.  Finally it was
// standardized in C99 and C++11:
//
// http://en.cppreference.com/w/cpp/numeric/math/isfinite
//
// The name was changed to `isfinite()`.  And conforming C99 and C++11
// compilers can omit the old versions, so one cannot necessarily fall back on
// the old versions still being there.  Yet the old versions don't have
// isfinite(), so those have to be worked around here as well.
//

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L // C99 or later
    #define FINITE isfinite
#elif CPLUSPLUS_11  // C++11 or later
    #define FINITE isfinite
#elif defined(__MINGW32__) || defined(__MINGW64__)
    #define FINITE isfinite // With --std==c++98 MinGW still has isfinite
#elif defined(_WIN32)  // 32-bit or 64-bit windows
    #define FINITE _finite // The usual answer for Windows
#else
    #define FINITE finite // The usual answer for POSIX
#endif


//=//// SLIGHTLY SAFER MIN AND MAX MACROS IN C++ //////////////////////////=//
//
// The standard definition in C for MIN and MAX uses preprocessor macros, and
// this has fairly notorious problems of double-evaluating anything with
// side-effects:
//
// https://stackoverflow.com/a/3437484
//
// Sadly, there's no C++ template magic for detecting if expressions have side
// effects that we can use to verify that the callsites as compiled in C are
// safe usages MIN and MAX.  This boils down to the fact that in the C++
// function model, "an int is an int", whether it's a literal or if it's the
// result of a function call:
//
// https://stackoverflow.com/questions/50667501/
//
// But what we can do is make a function that takes the duplicated arguments
// and compares them with each other.
//
// 1. It is common for MIN and MAX to be defined in C to macros; and equally
//    common to assume that undefining them and redefining them to something
//    that acts as it does in most codebases is "probably ok".  :-/
//
// 2. As mentioned above, no magic exists at time of writing that can help
//    enforce T1 and T2 didn't arise from potential-side-effect expressions.
//    `consteval` in C++20 can force compile-time evaluation, but that would
//    only allow MIN(10, 20).  Putting this here in case some future trickery
//    comes along.
//
// 3. In order to make it as similar to the C as possible, we make MIN and
//    MAX macros so they can be #undef'd or redefined (as opposed to just
//    naming the helper templated functions MIN and MAX).
//
#undef MIN  // common for these to be defined [1]
#undef MAX
#if NO_CPLUSPLUS_11 || NO_RUNTIME_CHECKS
    #define MIN(a,b) (((a) < (b)) ? (a) : (b))
    #define MAX(a,b) (((a) > (b)) ? (a) : (b))
#else
    template <
        typename T1,
        typename T2,
        typename std::enable_if<true>::type* = nullptr  // no magic checks [2]
    >
    inline auto cpp_min_helper(  // Note: constexpr can't assert in C++11
        T1 a, T1 aa, T2 b, T2 bb
    ) -> typename std::common_type<T1, T2>::type{
        assert(a == aa);
        assert(b == bb);
        return (a < b) ? a : b;
    }

    template <
        typename T1,
        typename T2,
        typename std::enable_if<true>::type* = nullptr  // no magic checks [2]
    >
    inline auto cpp_max_helper(  // Note: constexpr can't assert in C++11
        T1 a, T1 aa, T2 b, T2 bb
    ) -> typename std::common_type<T1, T2>::type{
        assert(a == aa);
        assert(b == bb);
        return (a > b) ? a : b;
    }

    #define MIN(a,b)  cpp_min_helper((a), (a), (b), (b))  // use macros [3]
    #define MAX(a,b)  cpp_max_helper((a), (a), (b), (b))
#endif


//=//// CONDITIONAL C++ NAME MANGLING MACROS //////////////////////////////=//
//
// When linking C++ code, different functions with the same name need to be
// discerned by the types of their parameters.  This means their name is
// "decorated" (or "mangled") from the fairly simple and flat convention of
// a C function.
//
// https://en.wikipedia.org/wiki/Name_mangling
// http://en.cppreference.com/w/cpp/language/language_linkage
//
// This also applies to global variables in some compilers (e.g. MSVC), and
// must be taken into account:
//
// https://stackoverflow.com/a/27939238/211160
//
// When built as C++, Ren-C must tell the compiler that functions/variables
// it exports to the outside world should *not* use C++ name mangling, so that
// they can be used sensibly from C.  But the instructions to tell it that
// are not legal in C.  This conditional macro avoids needing to put #ifdefs
// around those prototypes.
//
#if defined(__cplusplus)
    #define EXTERN_C extern "C"
#else
    // !!! There is some controversy on whether EXTERN_C should be a no-op in
    // a C build, or decay to the meaning of C's `extern`.  Notably, WinSock
    // headers from Microsoft use this "decays to extern" form:
    //
    // https://stackoverflow.com/q/47027062/
    //
    // Review if this should be changed to use an EXTERN_C_BEGIN and an
    // EXTERN_C_END style macro--which would be a no-op in the C build and
    // require manual labeling of `extern` on any exported variables.
    //
    #define EXTERN_C extern
#endif


//=//// CONST COPYING TYPE TRAIT //////////////////////////////////////////=//
//
// This is a simple trait which adds const to the first type if the second
// type is const.
//
#if CPLUSPLUS_11
    template<typename U,typename T>
    struct copy_const {
        using type = typename std::conditional<
            std::is_const<T>::value,
            typename std::add_const<U>::type,
            U
        >::type;
    };

    template<typename U,typename T>
    using copy_const_t = typename copy_const<U,T>::type;
#endif
