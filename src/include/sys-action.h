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
// Copyright 2012-2017 Rebol Open Source Contributors
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
    //
    // Returning boolean results is specially chosen as the 0 and 1 values,
    // so that a logic result can just be cast, as with R_FROM_BOOL().
    // See remarks on REBOOL about how it is ensured that TRUE is 1, and
    // that this is the standard for C++ bool conversion:
    //
    // http://stackoverflow.com/questions/2725044/
    //
    R_FALSE = 0, // => Init_Logic(D_OUT, FALSE); return R_OUT;
    R_TRUE = 1, // => Init_Logic(D_OUT, TRUE); return R_OUT;

    // Void and blank are also common results.
    //
    R_VOID, // => Init_Void(D_OUT); return R_OUT;
    R_BLANK, // => Init_Blank(D_OUT); return R_OUT;
    R_BAR, // Init_Bar(D_OUT); return R_OUT;

    // This means that the value in D_OUT is to be used as the return result.
    // Note that value starts as an END, and must be written to some other
    // value before this return can be used (checked by assert in debug build)
    //
    R_OUT,

    // See comments on VALUE_FLAG_THROWN about the migration of "thrownness"
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

    // If Do_Core gets back an R_REDO from a dispatcher, it will re-execute
    // the f->phase in the frame.  This function may be changed by the
    // dispatcher from what was originally called.
    //
    R_REDO_CHECKED, // check the types again, fill in exits
    R_REDO_UNCHECKED, // don't recheck types, just run next function in stack

    // EVAL is special because it stays at the frame level it is already
    // running, but re-evaluates.  In order to do this, it must protect its
    // argument during that evaluation, so it writes into the frame's
    // "eval cell".
    //
    R_REEVALUATE_CELL,
    R_REEVALUATE_CELL_ONLY,

    // See ACTION_FLAG_INVISIBLE...this is what any function with that flag
    // needs to return.
    //
    // It is also used by path dispatch when it has taken performing a
    // SET-PATH! into its own hands, but doesn't want to bother saying to
    // move the value into the output slot...instead leaving that to the
    // evaluator (as a SET-PATH! should always evaluate to what was just set)
    //
    R_INVISIBLE,

    // Path dispatch used to have a return value PE_SET_IF_END which meant
    // that the dispatcher itself should realize whether it was doing a path
    // get or set, and if it were doing a set then to write the value to set
    // into the target cell.  That means it had to keep track of a pointer to
    // a cell vs. putting the bits of the cell into the output.  This is now
    // done with a special REB_0_REFERENCE type which holds in its payload
    // a RELVAL and a specifier, which is enough to be able to do either a
    // read or a write, depending on the need.
    //
    // !!! See notes in %c-path.c of why the R3-Alpha path dispatch is hairier
    // than that.  It hasn't been addressed much in Ren-C yet, but needs a
    // more generalized design.
    //
    R_REFERENCE,

    // This is used in path dispatch, signifying that a SET-PATH! assignment
    // resulted in the updating of an immediate expression in pvs->out,
    // meaning it will have to be copied back into whatever reference cell
    // it had been in.
    //
    R_IMMEDIATE,

    // This is a signal that isn't accepted as a return value from a native,
    // so it can be used by common routines that return REB_R values and need
    // an "escape" code.
    //
    R_UNHANDLED,

    // Used as a signal from Do_Vararg_Op_May_Throw
    //
    R_END
};
typedef enum Reb_Result REB_R;

// Specially chosen 0 and 1 values for R_FALSE and R_TRUE enable this. 
//
inline static REB_R R_FROM_BOOL(REBOOL b) {
    return cast(REB_R, cast(int, b));
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
#if !defined(NDEBUG) && defined(CPLUSPLUS_11)
    struct REBSYM;

    struct OPT_REBSYM { // can only be converted to REBSYM, no comparisons
        enum REBOL_Symbols n;
        OPT_REBSYM (const REBSYM& sym);
        REBOOL operator==(enum REBOL_Symbols other) const {
            return n == other;
        }
        REBOOL operator!=(enum REBOL_Symbols other) const {
            return n != other;
        }

        REBOOL operator==(OPT_REBSYM &&other) const;
        REBOOL operator!=(OPT_REBSYM &&other) const;

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
            return n >= other;
        }
        REBOOL operator<=(enum REBOL_Symbols other) const {
            assert(other != SYM_0);
            return n <= other;
        }
        REBOOL operator>(enum REBOL_Symbols other) const {
            assert(other != SYM_0);
            return n > other;
        }
        REBOOL operator<(enum REBOL_Symbols other) const {
            assert(other != SYM_0);
            return n < other;
        }
        REBOOL operator==(enum REBOL_Symbols other) const {
            return n == other;
        }
        REBOOL operator!=(enum REBOL_Symbols other) const {
            return n != other;
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
    assert(a != SYM_0 and b != SYM_0);
    return cast(REBCNT, a) == cast(REBCNT, b);
}

// C function implementing a native ACTION!
//
typedef REB_R (*REBNAT)(REBFRM *frame_);
#define REBNATIVE(n) \
    REB_R N_##n(REBFRM *frame_)

// Type-Action-Function: implementing a "verb" ACTION! for a particular type
// (or class of types).
//
typedef REB_R (*REBTAF)(REBFRM *frame_, REBSYM verb);
#define REBTYPE(n) \
    REB_R T_##n(REBFRM *frame_, REBSYM verb)

// Port-Action-Function: for implementing "verb" ACTION!s on a PORT! class
// 
typedef REB_R (*REBPAF)(REBFRM *frame_, REBCTX *p, REBSYM verb);

// Path evaluator function
//
typedef REB_R (*REBPEF)(
    REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval
);

// "Routine INfo" was once a specialized C structure, now an ordinary Rebol
// REBARR pointer.
//
typedef REBARR REBRIN;
