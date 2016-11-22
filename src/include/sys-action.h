//
//  File: %sys-action.h
//  Summary: "Definition of action dispatchers"
//  Section: core
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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


// !!! Originally, REB_R was a REBCNT from reb-c.h (not this enumerated type
// containing its legal values).  That's because enums in C have no guaranteed
// size, yet Rebol wants to use known size types in its interfaces.
//
// However, there are other enums in %tmp-funcs.h, and the potential for bugs
// is too high to not let the C++ build check the types.  So for now, REB_R
// uses this enum.
//
enum Reb_Result {
    // Returning boolean results is specially chosen as the 0 and 1 values,
    // so that a logic result can just be cast, as with R_FROM_BOOL().
    // See remarks on REBOOL about how it is ensured that TRUE is 1, and
    // that this is the standard for C++ bool conversion:
    //
    // http://stackoverflow.com/questions/2725044/
    //
    R_FALSE = 0, // => SET_FALSE(D_OUT); return R_OUT;
    R_TRUE = 1, // => SET_TRUE(D_OUT); return R_OUT;

    // Void and blank are also common results.
    //
    R_VOID, // => SET_VOID(D_OUT); return R_OUT;
    R_BLANK, // => SET_BLANK(D_OUT); return R_OUT;

    // This means that the value in D_OUT is to be used as the return result.
    // Note that value starts as an END, and must be written to some other
    // value before this return can be used (checked by assert in debug build)
    //
    R_OUT,

    // By default, all return results will not have the VALUE_FLAG_UNEVALUATED
    // bit when they come back from a function.  To override that, this asks
    // the dispatch to clear the bit instead.  It should be noted that since
    // there is no meaningful way to carry the bit when copying values around
    // internally, this is only a useful bit to read on things that were
    // known to go directly through an evaluation step...e.g. arguments to
    // functions on their initial fulfillment.  So this is returned by the
    // QUOTE native (for instance).
    //
    R_OUT_UNEVALUATED,

    // See comments on OPT_VALUE_THROWN about the migration of "thrownness"
    // from being a property signaled to the evaluator.
    //
    // R_OUT_IS_THROWN is a test of that signaling mechanism.  It is currently
    // being kept in parallel with the THROWN() bit and ensured as matching.
    // Being in the state of doing a stack unwind will likely be knowable
    // through other mechanisms even once the thrown bit on the value is
    // gone...so it may not be the case that natives are asked to do their
    // own separate indication, so this may wind up replaced with R_OUT.  For
    // the moment it is good as a double-check.
    //
    R_OUT_IS_THROWN,

    // This is a return value in service of refinements like IF/BRANCHED?.
    // Since all dispatchers get END markers in the f->out slot (a.k.a. D_OUT)
    // then it can be used to tell if the output has been written "in band"
    // by a legal value or void.  This returns TRUE if D_OUT is not END,
    // and FALSE if it still is.
    //
    R_OUT_TRUE_IF_WRITTEN,

    // Similar to R_OUT_WRITTEN_Q, this converts an illegal END marker return
    // value in R_OUT to simply a void.
    //
    R_OUT_VOID_IF_UNWRITTEN,

    // If Do_Core gets back an R_REDO from a dispatcher, it will re-execute
    // the f->func in the frame.  This function may be changed by the
    // dispatcher from what was originally called.
    //
    R_REDO_CHECKED, // check the types again, fill in exits
    R_REDO_UNCHECKED, // don't bother checking, just run next function in stack

    // EVAL is special because it stays at the frame level it is already
    // running, but re-evaluates.  In order to do this, it must protect its
    // argument during that evaluation, so it writes into the frame's
    // "eval cell".
    //
    R_REEVALUATE,
    R_REEVALUATE_ONLY
};
typedef enum Reb_Result REB_R;

// Convenience function for getting behaviors like WHILE/LOOPED?", and
// doing the default thing--assuming END is being left in the D_OUT slot if
// the tested-for condition is not met.
//
inline static REB_R R_OUT_Q(REBOOL q) {
    if (q) return R_OUT_TRUE_IF_WRITTEN;
    return R_OUT_VOID_IF_UNWRITTEN;
}

// Specially chosen 0 and 1 values for R_FALSE and R_TRUE enable this. 
//
inline static REB_R R_FROM_BOOL(REBOOL b) {
    return cast(REB_R, b);
}

// R3-Alpha's concept was that all words got persistent integer values, which
// prevented garbage collection.  Ren-C only gives built-in words integer
// values--or SYMs--while others must be compared by pointers to their
// name or canon-name pointers.  A non-built-in symbol will return SYM_0 as
// its symbol, allowing it to fall through to defaults in case statements.
//
// Though it works fine for switch statements, it creates a problem if someone
// writes `VAL_WORD_SYM(a) == VAL_WORD_SYM(b)`, because all non-built-ins
// will appear to be equal.  It's a tricky enough bug to catch to warrant an
// extra check in C++ that disallows comparing SYMs with ==
//
#ifdef __cplusplus
    struct REBSYM;

    struct OPT_REBSYM { // can only be converted to REBSYM, no comparisons
        enum REBOL_Symbols n;
        OPT_REBSYM (const REBSYM& sym);
        REBOOL operator==(enum REBOL_Symbols other) const {
            return LOGICAL(n == other);
        }
        REBOOL operator!=(enum REBOL_Symbols other) const {
            return LOGICAL(n != other);
        }
    #if __cplusplus >= 201103L // http://stackoverflow.com/a/35399513/211160
        REBOOL operator==(OPT_REBSYM &&other) const;
        REBOOL operator!=(OPT_REBSYM &&other) const;
    #endif
        operator unsigned int() const {
            return cast(unsigned int, n);
        }
    };

    struct REBSYM { // acts like a REBOL_Symbol with no OPT_REBSYM compares
        enum REBOL_Symbols n;
        REBSYM () {}
        REBSYM (int n) : n (cast(enum REBOL_Symbols, n)) {}
        REBSYM (OPT_REBSYM opt_sym) : n (opt_sym.n) {}
        operator unsigned int() const {
            return cast(unsigned int, n);
        }
        REBOOL operator>=(enum REBOL_Symbols other) const {
            assert(other != SYM_0);
            return LOGICAL(n >= other);
        }
        REBOOL operator<=(enum REBOL_Symbols other) const {
            assert(other != SYM_0);
            return LOGICAL(n <= other);
        }
        REBOOL operator>(enum REBOL_Symbols other) const {
            assert(other != SYM_0);
            return LOGICAL(n > other);
        }
        REBOOL operator<(enum REBOL_Symbols other) const {
            assert(other != SYM_0);
            return LOGICAL(n < other);
        }
        REBOOL operator==(enum REBOL_Symbols other) const {
            return LOGICAL(n == other);
        }
        REBOOL operator!=(enum REBOL_Symbols other) const {
            return LOGICAL(n != other);
        }
        REBOOL operator==(REBSYM &other) const; // could be SYM_0!
        void operator!=(REBSYM &other) const; // could be SYM_0!
        REBOOL operator==(const OPT_REBSYM &other) const; // could be SYM_0!
        void operator!=(const OPT_REBSYM &other) const; // could be SYM_0!
    };

    inline OPT_REBSYM::OPT_REBSYM(const REBSYM &sym) : n (sym.n) {}
#else
    typedef enum REBOL_Symbols REBSYM;
    typedef enum REBOL_Symbols OPT_REBSYM; // act sameas REBSYM in C build
#endif

inline static REBOOL SAME_SYM_NONZERO(REBSYM a, REBSYM b) {
    assert(a != SYM_0 && b != SYM_0);
    return LOGICAL(cast(REBCNT, a) == cast(REBCNT, b));
}

// NATIVE! function
typedef REB_R (*REBNAT)(REBFRM *frame_);
#define REBNATIVE(n) \
    REB_R N_##n(REBFRM *frame_)

// ACTION! function (one per each DATATYPE!)
typedef REB_R (*REBACT)(REBFRM *frame_, REBSYM a);
#define REBTYPE(n) \
    REB_R T_##n(REBFRM *frame_, REBSYM action)

// PORT!-action function
typedef REB_R (*REBPAF)(REBFRM *frame_, REBCTX *p, REBSYM a);

// COMMAND! function
typedef REB_R (*CMD_FUNC)(REBCNT n, REBSER *args);

typedef struct Reb_Routine_Info REBRIN;
