//
//  File: %sys-path.h
//  Summary: "Definition of Structures for Path Processing"
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
// When a path like `a/(b + c)/d` is evaluated, it moves in steps.  The
// evaluative result of chaining the prior steps is offered as input to
// the next step.  The path evaluator `Eval_Path_Throws` delegates steps to
// type-specific "(P)ath (D)ispatchers" with names like PD_Context,
// PD_Array, etc.
//
// R3-Alpha left several open questions about the handling of paths.  One
// of the trickiest regards the mechanics of how to use a SET-PATH! to
// write data into native structures when more than one path step is
// required.  For instance:
//
//     >> gob/size
//     == 10x20
//
//     >> gob/size/x: 304
//     >> gob/size
//     == 10x304
//
// Because GOB! stores its size as packed bits that are not a full PAIR!,
// the `gob/size` path dispatch can't give back a pointer to a REBVAL* to
// which later writes will update the GOB!.  It can only give back a
// temporary value built from its internal bits.  So workarounds are needed,
// as they are for a similar situation in trying to set values inside of
// C arrays in STRUCT!.
//
// The way the workaround works involves allowing a SET-PATH! to run forward
// and write into a temporary value.  Then in these cases the temporary
// REBVAL is observed and used to write back into the native bits before the
// SET-PATH! evaluation finishes.  This means that it's not currently
// prohibited for the effect of a SET-PATH! to be writing into a temporary.
//
// Further, the `value` slot is writable...even when it is inside of the path
// that is being dispatched:
//
//     >> code: compose [(make set-path! [12-Dec-2012 day]) 1]
//     == [12-Dec-2012/day: 1]
//
//     >> do code
//
//     >> probe code
//     [1-Dec-2012/day: 1]
//
// Ren-C has largely punted on resolving these particular questions in order
// to look at "more interesting" ones.  However, names and functions have
// been updated during investigation of what was being done.
//

// Note that paths can be initialized with an array, which they will then
// take as immutable...or you can create a `/foo`-style path in a more
// optimized fashion using Refinify()

#define Init_Any_Path(v,k,a) \
    Init_Any_Path_At_Core((v), (k), (a), 0, nullptr)

#define Init_Path(v,a) \
    Init_Any_Path((v), REB_PATH, (a))

#define PVS_OPT_SETVAL(pvs) \
    pvs->special

#define PVS_IS_SET_PATH(pvs) \
    (PVS_OPT_SETVAL(pvs) != nullptr)

#define PVS_PICKER(pvs) \
    FRM_CELL(pvs)

inline static bool Get_Path_Throws_Core(
    REBVAL *out,
    const RELVAL *any_path,
    REBSPC *specifier
){
    return Eval_Path_Throws_Core(
        out,
        NULL, // not requesting symbol means refinements not allowed
        VAL_ARRAY(any_path),
        VAL_INDEX(any_path),
        Derive_Specifier(specifier, any_path),
        NULL, // not requesting value to set means it's a get
        0 // Name contains Get_Path_Throws() so it shouldn't be neutral
    );
}


inline static void Get_Path_Core(
    REBVAL *out,
    const RELVAL *any_path,
    REBSPC *specifier
){
    assert(ANY_PATH(any_path)); // *could* work on ANY_ARRAY(), actually

    if (Eval_Path_Throws_Core(
        out,
        NULL, // not requesting symbol means refinements not allowed
        VAL_ARRAY(any_path),
        VAL_INDEX(any_path),
        Derive_Specifier(specifier, any_path),
        NULL, // not requesting value to set means it's a get
        EVAL_FLAG_NO_PATH_GROUPS
    )){
        panic (out); // shouldn't be possible... no executions!
    }
}


inline static bool Set_Path_Throws_Core(
    REBVAL *out,
    const RELVAL *any_path,
    REBSPC *specifier,
    const REBVAL *setval
){
    assert(ANY_PATH(any_path)); // *could* work on ANY_ARRAY(), actually

    return Eval_Path_Throws_Core(
        out,
        NULL, // not requesting symbol means refinements not allowed
        VAL_ARRAY(any_path),
        VAL_INDEX(any_path),
        Derive_Specifier(specifier, any_path),
        setval,
        0 // Name contains Set_Path_Throws() so it shouldn't be neutral
    );
}


inline static void Set_Path_Core(
    const RELVAL *any_path,
    REBSPC *specifier,
    const REBVAL *setval,
    bool enfix
){
    assert(ANY_PATH(any_path)); // *could* work on ANY_ARRAY(), actually

    // If there's no throw, there's no result of setting a path (hence it's
    // not in the interface)
    //
    DECLARE_LOCAL (out);

    REBFLGS flags = EVAL_FLAG_NO_PATH_GROUPS;
    if (enfix)
        flags |= EVAL_FLAG_SET_PATH_ENFIXED;

    if (Eval_Path_Throws_Core(
        out,
        NULL, // not requesting symbol means refinements not allowed
        VAL_ARRAY(any_path),
        VAL_INDEX(any_path),
        Derive_Specifier(specifier, any_path),
        setval,
        flags
    )){
        panic (out); // shouldn't be possible, no executions!
    }
}


// Ren-C has no REFINEMENT! datatype, so `/foo` is a PATH!, which generalizes
// to where `/foo/bar` is a PATH! as well, etc.
//
// !!! Optimizations are planned to allow single element paths to fit in just
// *one* array cell.  This will make use of the fourth header byte, to
// encode when the type byte is a container for what is inside.  Use of this
// routine to mutate cells into refinements marks places where that will
// be applied.
//
inline static REBVAL *Refinify(REBVAL *v) {
    //
    // Making something into a refinement is not a generically applicable
    // operation like Quotify that you can do any number of times.  Note
    // you can't put paths in paths in the first place.
    //
    assert(CELL_KIND(VAL_UNESCAPED(v)) != REB_PATH);

    REBARR *a = Make_Arr(2);
    Init_Blank(Alloc_Tail_Array(a));
    Move_Value(Alloc_Tail_Array(a), v);
    return Init_Path(v, a);
}

inline static bool IS_REFINEMENT(const RELVAL *v) {
    return IS_PATH(v)
        and VAL_LEN_HEAD(v) == 2
        and IS_BLANK(VAL_ARRAY_AT_HEAD(v, 0))
        and IS_WORD(VAL_ARRAY_AT_HEAD(v, 1));
}

inline static REBSTR *VAL_REFINEMENT_SPELLING(const RELVAL *v) {
    assert(IS_REFINEMENT(v));
    return VAL_WORD_SPELLING(VAL_ARRAY_AT_HEAD(v, 1));
}
