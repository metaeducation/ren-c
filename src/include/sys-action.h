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


// enums in C have no guaranteed size, yet Rebol wants to use known size
// types in its interfaces.  Hence REB_R is a REBCNT from reb-c.h (and not
// this enumerated type containing its legal values).
enum {
    R_OUT = 0,

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

    // This is a return value in service of the /? functions.  Since all
    // dispatchers receive an END marker in the f->out slot (a.k.a. D_OUT)
    // then it can be used to tell if the output has been written "in band"
    // by a legal value or void.  This returns TRUE if D_OUT is not END,
    // and FALSE if it still is.
    //
    R_OUT_TRUE_IF_WRITTEN,

    // Similar to R_OUT_WRITTEN_Q, this converts an illegal END marker return
    // value in R_OUT to simply a void.
    //
    R_OUT_VOID_IF_UNWRITTEN,

    // !!! These R_ values are somewhat superfluous...and actually inefficient
    // because they have to be checked by the caller in a switch statement
    // to take the equivalent action.  They have a slight advantage in
    // hand-written C code for making it more clear that if you have used
    // the D_OUT return slot for temporary work that you explicitly want
    // to specify another result...this cannot be caught by the REB_TRASH
    // trick for detecting an unwritten D_OUT.
    //
    R_VOID, // => SET_VOID(D_OUT); return R_OUT;
    R_BLANK, // => SET_BLANK(D_OUT); return R_OUT;
    R_TRUE, // => SET_TRUE(D_OUT); return R_OUT;
    R_FALSE, // => SET_FALSE(D_OUT); return R_OUT;

    // If Do_Core gets back an R_REDO from a dispatcher, it will re-execute
    // the f->func in the frame.  This function may be changed by the
    // dispatcher from what was originally called.
    //
    R_REDO
};
typedef REBCNT REB_R;

// Convenience function for getting the "/?" behavior if it is enabled, and
// doing the default thing--assuming END is being left in the D_OUT slot
//
inline static REB_R R_OUT_Q(REBOOL q) {
    if (q) return R_OUT_TRUE_IF_WRITTEN;
    return R_OUT_VOID_IF_UNWRITTEN;
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
typedef REB_R (*REBNAT)(struct Reb_Frame *frame_);
#define REBNATIVE(n) \
    REB_R N_##n(struct Reb_Frame *frame_)

// ACTION! function (one per each DATATYPE!)
typedef REB_R (*REBACT)(struct Reb_Frame *frame_, REBSYM a);
#define REBTYPE(n) \
    REB_R T_##n(struct Reb_Frame *frame_, REBSYM action)

// PORT!-action function
typedef REB_R (*REBPAF)(struct Reb_Frame *frame_, REBCTX *p, REBSYM a);

// COMMAND! function
typedef REB_R (*CMD_FUNC)(REBCNT n, REBSER *args);

typedef struct Reb_Routine_Info REBRIN;
