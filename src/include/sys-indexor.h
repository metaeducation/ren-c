//
//  File: %sys-indexor.h
//  Summary: {Definitions for "INDEX-OR-a-flag", and C++ supplementary checks}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// R3-Alpha wished to encode "magic values" into the integer index which is
// used for stepping through arrays.  Hence 0, 1, 2, etc. would be normal
// indices, but 2,147,483,647 and 2,147,483,648 would be "magic" values
// (for instance) to indicate a status result of THROWN or END of input.
//
// Ren-C gave this encoded value a separate REBIXO type and the name "Indexor"
// to mean "Index-OR-a-Flag".  In the C build this is the same old unsigned
// integer.  But the C++ debug build uses a class that at compile time checks
// to make sure no flag value is implicitly converted to a REBCNT, and at
// runtime checks that explicit casts don't violate the rule either.
//
// !!! This could be enhanced so that the REBIXO would keep track of whether
// or not it had been tested for THROWN_FLAG and END_FLAG.  However, this
// would take more bits out of the index, if a REBIXO seeks to be the same
// size and bit pattern in the C and C++ build.  (Losing the has-been-checked
// bits would be less intrusive in a 64-bit build.)
//
// The extra code and checking is only paid for in the *debug* C++ build.
// See "Static-and-Dynamic-Analysis-in-the-Cpp-Build" on:
//
// https://github.com/metaeducation/ren-c/wiki/
//

#if defined(NDEBUG) || !defined(CPLUSPLUS_11)
    typedef REBUPT REBIXO;

    #define END_FLAG 0x80000000  // end of block as index
    #define THROWN_FLAG (END_FLAG - 0x75) // throw as an index

    // The VA_LIST_FLAG is the index used when a C va_list pointer is input.
    // Because access to a `va_list` is strictly increasing through va_arg(),
    // there is no way to track an index; fetches are indexed automatically
    // and sequentially without possibility for mutation of the list.  Should
    // this index be used it will always be the index of a DO_NEXT until
    // either an END_FLAG or a THROWN_FLAG is reached.
    //
    #define VA_LIST_FLAG (END_FLAG - 0xBD)

    // These are not actually used with REBIXO, but have a similar purpose...
    // fold a flag into an integer (like a std::optional<REBCNT>).
    //
    #define NOT_FOUND ((REBCNT)-1)
    #define UNKNOWN   ((REBCNT)-1)
#else
    class NOT_FOUND_t {
    public:
        NOT_FOUND_t () {} // clang won't initialize const object w/o this
        operator REBCNT() const {
            return ((REBCNT)-1);
        }
    };
    const NOT_FOUND_t NOT_FOUND;
    const NOT_FOUND_t UNKNOWN;

    class REBIXO {
        //
        // If an equality or inequality test is done against THROWN_FLAG or
        // END_FLAG, this mutates the bits to indicate the test has been
        // done.  Coercion to a plain integer will not be allowed unless both
        // have been tested for.
        //
        REBUPT bits;

    public:
        // Make sure you can't assign or compare from NOT_FOUND or UNKNOWN
        //
        REBIXO (NOT_FOUND_t const &) = delete;
        void operator=(NOT_FOUND_t const &) = delete;
        int operator==(NOT_FOUND_t const &rhs) const = delete;
        int operator!=(NOT_FOUND_t const &rhs) const = delete;

    public:
        REBIXO () {} // simulate C uninitialization
        REBIXO (REBCNT bits) : bits (bits) {
            assert(bits != ((REBCNT)-1)); // not with REBIXO!
        }

        void operator=(REBCNT rhs) {
            assert(rhs != ((REBCNT)-1)); // not with REBIXO!
            bits = rhs;
        }
        int operator==(REBIXO const &rhs) const { return bits == rhs.bits; }
        int operator!=(REBIXO const &rhs) const { return !((*this) == rhs); }

        // Basic check: whenever one tries to get an actual REBCNT out of
        // an indexor, it is asserted not to be a magic value.  Called by
        // the math operations, as well as any explicit `cast(REBCNT, indexor)`
        //
        explicit operator REBCNT() const {
            //
            // Individual asserts so the line number tells you which it is.
            //
            assert(bits != 0x80000000); // END_FLAG
            assert(bits != 0x80000000 - 0x75); // THROWN_FLAG 
            assert(bits != 0x80000000 - 0xBD); // VA_LIST_FLAG
        #if !defined(NDEBUG)
            assert(bits != 0x80000000 - 0xAE); // TRASHED_INDEX
        #endif
            return bits;
        }

        // Subset of operations that are exported to be legal to perform with
        // an unsigned integer and an indexor.  Comparisons for equality and
        // addition and subtraction are allowed.  While more operations could
        // be added, the best course of action is generally that if one is
        // to do a lot of math on an indexor it is not a special value...so it
        // should be extracted by casting to a REBCNT.
        //
        friend int operator==(REBCNT lhs, const REBIXO &rhs) {
            assert(lhs != UNKNOWN && lhs != NOT_FOUND);
            return lhs == rhs.bits;
        }
        friend int operator!=(REBCNT lhs, const REBIXO &rhs) {
            return !(lhs == rhs);
        }
        int operator<(REBCNT rhs) const {
            return cast(REBCNT, *this) < rhs;
        }
        friend int operator<(REBCNT lhs, const REBIXO &rhs) {
            return lhs < rhs.bits;
        }
        int operator>(REBCNT rhs) const {
            return cast(REBCNT, *this) > rhs;
        }
        friend int operator>(REBCNT lhs, const REBIXO &rhs) {
            return lhs > rhs.bits;
        }
        int operator<=(REBCNT rhs) const {
            return cast(REBCNT, *this) <= rhs;
        }
        friend int operator<=(REBCNT lhs, const REBIXO &rhs) {
            return lhs <= rhs.bits;
        }
        REBCNT operator+(REBCNT rhs) const {
            return cast(REBCNT, *this) + rhs;
        }
        friend REBCNT operator+(REBCNT lhs, const REBIXO &rhs) {
            return rhs + lhs;
        }
        REBCNT operator-(REBCNT rhs) const {
            return cast(REBCNT, *this) - rhs;
        }
        friend REBCNT operator-(REBCNT lhs, const REBIXO &rhs) {
            return rhs - lhs;
        }
        REBCNT operator*(REBCNT rhs) const {
            return cast(REBCNT, *this) * rhs;
        }
        friend REBCNT operator*(REBCNT lhs, const REBIXO &rhs) {
            return rhs * lhs;
        }

        REBIXO& operator++ () {
            bits = cast(REBCNT, *this) + 1; // cast ensures no END, THROWN
            return *this;
        }
        REBIXO& operator-- () {
            assert(bits != 0); // additional check
            bits = cast(REBCNT, *this) - 1; // cast ensures no END, THROWN
            return *this;
        }
    };

    const REBIXO END_FLAG (0x80000000);
    const REBIXO THROWN_FLAG (0x80000000 - 0x75);
    const REBIXO VA_LIST_FLAG (0x80000000 - 0xBD);

    // We want the C++ class to be the same size and compatible bit patterns
    // to the C build, so their binary code works together.
    //
    static_assert(
        sizeof(REBIXO) == sizeof(REBUPT), "REBIXO size must equal REBUPT"
    );
#endif

// This is used internally in frames in the debug build when the index
// does not apply (e.g. END, THROWN, VA_LIST)
//
#if !defined(NDEBUG)
    #define TRASHED_INDEX (0x80000000 - 0xAE)
#endif
