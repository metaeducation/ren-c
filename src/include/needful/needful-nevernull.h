//
//  file: %needful-nevernull.h
//  summary: "Helper for ensuring a pointer is never null"
//  homepage: <needful homepage TBD>
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2025 hostilefork.com
//
// Licensed under the MIT License
//
// https://en.wikipedia.org/wiki/MIT_License
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// This came in handly for a debugging scenario.  But because it uses deep
// voodoo to accomplish its work (like overloading -> and &), it interferes
// with more important applications of that voodoo.  So it shouldn't be used
// on types that depend on that (like Cell pointers).
//

#if (! CHECK_NEVERNULL_TYPEMACRO)
    #define NeverNull(type) \
        type
#else
    template <typename P>
    class NeverNullEnforcer {  // named so error message hints what's wrong
        typedef typename std::remove_pointer<P>::type T;
        P p;

      public:
        NeverNullEnforcer () : p () {}
        NeverNullEnforcer (P & p) : p (p) {
            assert(p != nullptr);
        }
        T& operator*() { return *p; }
        P operator->() { return p; }
        operator P() { return p; }
        P operator= (const P rhs) {  // if it returned reference, loses check
            assert(rhs != nullptr);
            this->p = rhs;
            return p;
        }
    };

    #define NeverNull(type) \
        NeverNullEnforcer<type>

  #if NEEDFUL_USES_CORRUPT_HELPER
    template<typename T>
    struct CorruptHelper<NeverNullEnforcer<T>> {
      static void corrupt(NeverNullEnforcer<T>& nn) {
        Corrupt_If_Needful(nn.p);
     }
    };
  #endif
#endif
