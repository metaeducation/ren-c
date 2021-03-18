//
//  File: %sys-rebfed.h
//  Summary: {REBFED Structure Frame Definition}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2020 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This declares the structure used by feeds, for use in other structs.
// See %sys-feed.h for a higher-level description.
//


#define FEED_MASK_DEFAULT NODE_FLAG_NODE

#define FEED_FLAG_0_IS_TRUE \
    FLAG_LEFT_BIT(0)
STATIC_ASSERT(FEED_FLAG_0_IS_TRUE == NODE_FLAG_NODE);

#define FEED_FLAG_1_IS_FALSE \
    FLAG_LEFT_BIT(1)
STATIC_ASSERT(FEED_FLAG_1_IS_FALSE == NODE_FLAG_FREE);


//=//// FEED_FLAG_DEFERRING_ENFIX /////////////////////////////////////////=//
//
// Defer notes when there is a pending enfix operation that was seen while an
// argument was being gathered, that decided not to run yet.  It will run only
// if it turns out that was the last argument that was being gathered...
// otherwise it will error.
//
//    if 1 [2] then [3]     ; legal
//    if 1 then [2] [3]     ; **error**
//    if (1 then [2]) [3]   ; legal, arguments weren't being gathered
//
// This flag is marked on a parent frame by the argument fulfillment the
// first time it sees a left-deferring operation like a THEN or ELSE, and is
// used to decide whether to report an error or not.
//
// (At one point, mechanics were added to make the second case not an
// error.  However, this gave the evaluator complex properties of re-entry
// that made its behavior harder to characterize.  This means that only a
// flag is needed, vs complex marking of a parameter to re-enter eval with.)
//
#define FEED_FLAG_DEFERRING_ENFIX \
    FLAG_LEFT_BIT(2)


//=//// FEED_FLAG_BARRIER_HIT /////////////////////////////////////////////=//
//
// Evaluation of arguments can wind up seeing a barrier and "consuming" it.
// This is true of a BAR!, but also GROUP!s which have no effective content:
//
//    >> 1 + (comment "vaporizes, but disrupts like a BAR! would") 2
//    ** Script Error: + is missing its value2 argument
//
// But the evaluation will advance the frame.  So if a function has more than
// one argument it has to remember that one of its arguments saw a "barrier",
// otherwise it would receive an end signal on an earlier argument yet then
// get a later argument fulfilled.
//
#define FEED_FLAG_BARRIER_HIT \
    FLAG_LEFT_BIT(3)


//=//// FEED_FLAG_NO_LOOKAHEAD ////////////////////////////////////////////=//
//
// Infix functions may (depending on the #tight or non-tight parameter
// acquisition modes) want to suppress further infix lookahead while getting
// a function argument.  This precedent was started in R3-Alpha, where with
// `1 + 2 * 3` it didn't want infix `+` to "look ahead" past the 2 to see the
// infix `*` when gathering its argument, that was saved until the `1 + 2`
// finished its processing.
//
#define FEED_FLAG_NO_LOOKAHEAD \
    FLAG_LEFT_BIT(4)


//=//// FEED_FLAG_NEXT_ARG_FROM_OUT ///////////////////////////////////////=//
//
// When processing something like enfix, the output cell of a frame is the
// place to look for the "next" value.  This setting has to be managed
// carefully in recursion, because the recursion must preserve the same
// notion of what the "out" cell is for the frame.
//
// !!! It may be better to think of this as an EVAL_FLAG, but those per-frame
// flags are running short.  Once this setting is on a feed, it has to be
// consumed or there will be an error.
//
#define FEED_FLAG_NEXT_ARG_FROM_OUT \
    FLAG_LEFT_BIT(5)


//=//// FEED_FLAG_TOOK_HOLD ///////////////////////////////////////////////=//
//
// If a feed takes SERIES_INFO_HOLD on an array it is enumerating, it has to
// remember that it did so it can release it when it is done processing.
// Note that this has to be a flag on the frame, not the feed--as a feed can
// be shared among many frames.
//
// !!! This is undermined by work in stackless, where a single bit is not
// sufficient since the stacks do not cleanly unwind:
//
// https://forum.rebol.info/t/1317
//
#define FEED_FLAG_TOOK_HOLD \
    FLAG_LEFT_BIT(6)


//=//// BITS 8...15 ARE THE QUOTING LEVEL /////////////////////////////////=//

// There was significant deliberation over what the following code should do:
//
//     REBVAL *word = rebValue("'print");
//     REBVAL *type = rebValue("type of", word);
//
// If the WORD! is simply spliced into the code and run, then that will be
// an error.  It would be as if you had written:
//
//     do compose [type of (word)]
//
// It may seem to be more desirable to pretend you had fetched word from a
// variable, as if the code had been Rebol.  The illusion could be given by
// automatically splicing quotes, but doing this without being asked creates
// other negative side effects:
//
//     REBVAL *x = rebInteger(10);
//     REBVAL *y = rebInteger(20);
//     REBVAL *coordinate = rebValue("[", x, y, "]");
//
// You don't want to wind up with `['10 '20]` in that block.  So automatic
// splicing with quotes is fraught with problems.  Hence you should use the
// rebQ() operator to add quoting levels when needed, or use the `@` operator
// as part of a string:
//
//     REBVAL *type = rebValue("type of", rebQ(word));
//     REBVAL *type = rebValue("type of @", word);
//
// rebQ() and rebU()  are generalized so that one may add and drop quoting from
// splices on a feed via ranges, countering any additions via rebQ() with a
// corresponding rebU().  This is kept within reason at up to 255 levels
// in a byte, and that byte is in the feed flags in the second byte (where
// it is least likely to be needed to line up with cell bits etc.)  Being in
// the flags means it can be initialized with them in one assignment if
// it does not change.
//

#define FLAG_QUOTING_BYTE(quoting) \
    FLAG_SECOND_BYTE(quoting)


//=//// FEED_FLAG_CONST ///////////////////////////////////////////////////=//
//
// The user is able to flip the constness flag explicitly with the CONST and
// MUTABLE functions explicitly.  However, if a feed has FEED_FLAG_CONST,
// the system imposes it's own constness as part of the "wave of evaluation"
// it does.  While this wave starts out initially with frames demanding const
// marking, if it ever gets flipped, it will have to encounter an explicit
// CONST marking on a value before getting flipped back.
//
// (This behavior is designed to permit switching into a "mode" that is
//
#define FEED_FLAG_CONST \
    FLAG_LEFT_BIT(30)
STATIC_ASSERT(FEED_FLAG_CONST == CELL_FLAG_CONST);


#if !defined __cplusplus
    #define FEED(f) f
#else
    #define FEED(f) static_cast<REBFED*>(f)
#endif

#define SET_FEED_FLAG(f,name) \
    (FEED(f)->flags.bits |= FEED_FLAG_##name)

#define GET_FEED_FLAG(f,name) \
    ((FEED(f)->flags.bits & FEED_FLAG_##name) != 0)

#define CLEAR_FEED_FLAG(f,name) \
    (FEED(f)->flags.bits &= ~FEED_FLAG_##name)

#define NOT_FEED_FLAG(f,name) \
    ((FEED(f)->flags.bits & FEED_FLAG_##name) == 0)


#define TRASHED_INDEX ((REBLEN)(-3))


struct Reb_Feed {
    union Reb_Header flags;  // quoting level included

    // This is the "prefetched" value being processed.  Entry points to the
    // evaluator must load a first value pointer into it...which for any
    // successive evaluations will be updated via Fetch_Next_In_Frame()--which
    // retrieves values from arrays or va_lists.  But having the caller pass
    // in the initial value gives the option of that value being out of band.
    //
    // (Hence if one has the series `[[a b c] [d e]]` it would be possible to
    // have an independent path value `append/only` and NOT insert it in the
    // series, yet get the effect of `append/only [a b c] [d e]`.  This only
    // works for one value, but is a convenient no-cost trick for apply-like
    // situations...as insertions usually have to "slide down" the values in
    // the series and may also need to perform alloc/free/copy to expand.
    // It also is helpful since in C, variadic functions must have at least
    // one non-variadic parameter...and one might want that non-variadic
    // parameter to be blended in with the variadics.)
    //
    // !!! Review impacts on debugging; e.g. a debug mode should hold onto
    // the initial value in order to display full error messages.
    //
    // Note: It was considered that this might be `option(const RELVAL*)`
    // if an end state, which would be faster to test than IS_END() at
    // callsites (no dereference needd) and could also help document that
    // a function was willing to take an "end" with `option` on an argument.
    // But the need to `unwrap(f_value)` constantly is annoying, and the
    // fact that the API uses nullptr to denote NULLED cells increases the
    // risk for confusion.  Plus, IS_END() is just more clear to what is
    // actually happening, and makes it possible to fold with KIND3Q_BYTE
    // testing.  Having tried the idea of using nullptr for END here, it's
    // definitely less clear...and may not even be faster.
    //
    const RELVAL *value;  // is never nullptr (ends w/END cells or rebEND)
    // Note: The specifier comes from FEED_SPECIFIER()

    //=//// ^-- be sure above fields align cells below to 64-bits --v /////=//
    // (two intptr_t sized things should take care of it on both 32/64-bit) //

    // Sometimes the frame can be advanced without keeping track of the
    // last cell.  And sometimes the last cell lives in an array that is
    // being held onto and read only, so its pointer is guaranteed to still
    // be valid after a fetch.  But there are cases where values are being
    // read from transient sources that disappear as they go...if that is
    // the case, and lookback is needed, it is written into this cell.
    //
    RELVAL lookback;

    // When feeding cells from a variadic, those cells may wish to mutate the
    // value in some way... e.g. to add a quoting level.  Rather than
    // complicate the evaluator itself with flags and switches, each frame
    // has a holding cell which can optionally be used as the pointer that
    // is returned by Fetch_Next_in_Frame(), where arbitrary mutations can
    // be applied without corrupting the value they operate on.
    //
    RELVAL fetched;

    // Feeds are maintained in REBSER-sized "splice" units.  This is big
    // enough for a REBVAL to hold an array and an index, but it also lets
    // you point to other singulars that can hold arrays and indices.
    //
    // If values are being sourced from an array, this holds the pointer to
    // that array.  By knowing the array it is possible for error and debug
    // messages to reach backwards and present more context of where the
    // error is located.
    //
    // This holds the index of the *next* item in the array to fetch as
    // f->value for processing.  It's invalid if the frame is for a C va_list.
    //
    // This is used for relatively bound words to be looked up to become
    // specific.  Typically the specifier is extracted from the payload of the
    // ANY-ARRAY! value that provided the source.array for the call to DO.
    // It may also be NULL if it is known that there are no relatively bound
    // words that will be encountered from the source--as in va_list calls.
    //
    REBSER singular;

    // There is a lookahead step to see if the next item in an array is a
    // WORD!.  If so it is checked to see if that word is a "lookback word"
    // (e.g. one that refers to an ACTION! value set with SET/ENFIX).
    // Performing that lookup has the same cost as getting the variable value.
    // Considering that the value will need to be used anyway--infix or not--
    // the pointer is held in this field for WORD!s.
    //
    // However, reusing the work is not possible in the general case.  For
    // instance, this would cause a problem:
    //
    //     obj: make object! [x: 10]
    //     foo: does [append obj [y: 20]]
    //     do in obj [foo x]
    //                   ^-- consider the moment of lookahead, here
    //
    // Before foo is run, it will fetch x to ->gotten, and see that it is not
    // a lookback function.  But then when it runs foo, the memory location
    // where x had been found before may have moved due to expansion.
    //
    // Basically any function call invalidates ->gotten, as does obviously any
    // Fetch_Next_In_Frame (because the position changes).  So it has to be
    // nulled out fairly often, and checked for null before reuse.
    //
    // !!! Review how often gotten has hits vs. misses, and what the benefit
    // of the feature actually is.
    //
    option(const REBVAL*) gotten;

  #if defined(DEBUG_EXPIRED_LOOKBACK)
    //
    // On each call to Fetch_Next_In_Feed, it's possible to ask it to give
    // a pointer to a cell with equivalent data to what was previously in
    // f->value, but that might not be f->value.  So for all practical
    // purposes, one is to assume that the f->value pointer died after the
    // fetch.  If clients are interested in doing "lookback" and examining
    // two values at the same time (or doing a GC and expecting to still
    // have the old f->current work), then they must not use the old f->value
    // but request the lookback pointer from Fetch_Next_In_Frame().
    //
    // To help stress this invariant, frames will forcibly expire REBVAL
    // cells, handing out disposable lookback pointers on each eval.
    //
    // !!! Test currently leaks on shutdown, review how to not leak.
    //
    RELVAL *stress;
  #endif

  #ifdef DEBUG_COUNT_TICKS
    REBTCK tick;
  #endif
};
