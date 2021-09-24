//
//  File: %sys-feed.h
//  Summary: {Accessors and Argument Pushers/Poppers for Function Call Frames}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2019 Ren-C Open Source Contributors
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
// A "Feed" represents an abstract source of Rebol values, which only offers
// a guarantee of being able to have two sequential values in the feed as
// having valid pointers at one time.  The main pointer is the feed's value
// (feed->value), and to be able to have another pointer to the previous
// value one must request a "lookback" at the time of advancing the feed.
//
// One reason for the feed's strict nature is that it offers an interface not
// just to Rebol BLOCK!s and other arrays, but also to variadic lists such
// as C's va_list...in a system which also allows the mixure of portions of
// UTF-8 string source text.  C's va_list does not retain a memory of the
// past, so once va_arg() is called it forgets the previous value...and
// since values may also be fabricated from text it can get complicated.
//
// Another reason for the strictness is to help rein in the evaluator design
// to keep it within a certain boundary of complexity.


#define FEED_SINGULAR(feed)     ARR(&(feed)->singular)
#define FEED_SINGLE(feed)       mutable_SER_CELL(&(feed)->singular)

#define LINK_Splice_TYPE        REBARR*
#define LINK_Splice_CAST        ARR
#define HAS_LINK_Splice         FLAVOR_FEED

#define MISC_Pending_TYPE       const RELVAL*
#define MISC_Pending_CAST       (const RELVAL*)
#define HAS_MISC_Pending        FLAVOR_FEED


#define FEED_SPLICE(feed) \
    LINK(Splice, &(feed)->singular)

// This contains an IS_END() marker if the next fetch should be an attempt
// to consult the va_list (if any).  That end marker may be resident in
// an array, or if it's a plain va_list source it may be the global END.
//
#define FEED_PENDING(feed) \
    MISC(Pending, &(feed)->singular)

#define FEED_IS_VARIADIC(feed)  IS_COMMA(FEED_SINGLE(feed))

#define FEED_VAPTR_POINTER(feed)    PAYLOAD(Comma, FEED_SINGLE(feed)).vaptr
#define FEED_PACKED(feed)           PAYLOAD(Comma, FEED_SINGLE(feed)).packed

inline static option(va_list*) FEED_VAPTR(REBFED *feed)
  { return FEED_VAPTR_POINTER(feed); }



// For performance, we always get the specifier from the same location, even
// if we're not using an array.  So for the moment, that means using a
// COMMA! (which for technical reasons has a nullptr binding and is thus
// always SPECIFIED).  However, VAL_SPECIFIER() only runs on arrays, so
// we sneak past that by accessing the node directly.
//
#define FEED_SPECIFIER(feed) \
    ARR(BINDING(FEED_SINGLE(feed)))

#define FEED_ARRAY(feed) \
    VAL_ARRAY(FEED_SINGLE(feed))

#define FEED_INDEX(feed) \
    VAL_INDEX_UNBOUNDED(FEED_SINGLE(feed))


// Most calls to Fetch_Next_In_Frame() are no longer interested in the
// cell backing the pointer that used to be in f->value (this is enforced
// by a rigorous test in DEBUG_EXPIRED_LOOKBACK).  Special care must be
// taken when one is interested in that data, because it may have to be
// moved.  So current can be returned from Fetch_Next_In_Frame_Core().

inline static const RELVAL *Lookback_While_Fetching_Next(REBFRM *f) {
  #if DEBUG_EXPIRED_LOOKBACK
    if (feed->stress) {
        RESET(feed->stress);
        free(feed->stress);
        feed->stress = nullptr;
    }
  #endif

    assert(READABLE(f->feed->value));  // ensure cell

    // f->value may be synthesized, in which case its bits are in the
    // `f->feed->fetched` cell.  That synthesized value would be overwritten
    // by another fetch, which would mess up lookback...so we cache those
    // bits in the lookback cell in that case.
    //
    // The reason we do this conditionally isn't just to avoid moving 4
    // platform pointers worth of data.  It's also to keep from reifying
    // array cells unconditionally with Derelativize().  (How beneficial
    // this is currently kind of an unknown, but in the scheme of things it
    // seems like it must be something favorable to optimization.)
    //
    const RELVAL *lookback;
    if (f->feed->value == &f->feed->fetched) {
        Move_Cell_Core(
            &f->feed->lookback,
            SPECIFIC(&f->feed->fetched),
            CELL_MASK_ALL
        );
        lookback = &f->feed->lookback;
    }
    else
        lookback = f->feed->value;

    Fetch_Next_In_Feed(f->feed);

  #if DEBUG_EXPIRED_LOOKBACK
    if (preserve) {
        f->stress = cast(RELVAL*, malloc(sizeof(RELVAL)));
        memcpy(f->stress, *opt_lookback, sizeof(RELVAL));
        lookback = f->stress;
    }
  #endif

    return lookback;
}

#define Fetch_Next_Forget_Lookback(f) \
    Fetch_Next_In_Feed(f->feed)


// This code is shared by Literal_Next_In_Feed(), and used without a feed
// advancement in the inert branch of the evaluator.  So for something like
// `repeat 2 [append [] 10]`, the steps are:
//
//    1. REPEAT defines its body parameter as <const>
//    2. When REPEAT runs Do_Any_Array_At_Throws() on the const ARG(body), the
//       frame gets FEED_FLAG_CONST due to the CELL_FLAG_CONST.
//    3. The argument to append is handled by the inert processing branch
//       which moves the value here.  If the block wasn't made explicitly
//       mutable (e.g. with MUTABLE) it takes the flag from the feed.
//
inline static void Inertly_Derelativize_Inheriting_Const(
    REBVAL *out,
    const RELVAL *v,
    REBFED *feed
){
    Derelativize(out, v, FEED_SPECIFIER(feed));
    SET_CELL_FLAG(out, UNEVALUATED);
    if (NOT_CELL_FLAG(v, EXPLICITLY_MUTABLE))
        out->header.bits |= (feed->flags.bits & FEED_FLAG_CONST);
}

inline static void Literal_Next_In_Feed(REBVAL *out, struct Reb_Feed *feed) {
    Inertly_Derelativize_Inheriting_Const(out, feed->value, feed);
    Fetch_Next_In_Feed(feed);
}


inline static REBFED* Alloc_Feed(void) {
    REBFED* feed = cast(REBFED*, Alloc_Node(FED_POOL));
  #if DEBUG_COUNT_TICKS
    feed->tick = TG_Tick;
  #endif

    Init_Trash(Prep_Cell(&feed->fetched));
    Init_Trash(Prep_Cell(&feed->lookback));

    REBSER *s = &feed->singular;  // SER() not yet valid
    s->leader.bits = NODE_FLAG_NODE | FLAG_FLAVOR(FEED);
    SER_INFO(s) = SERIES_INFO_MASK_NONE;
    Prep_Cell(FEED_SINGLE(feed));
    mutable_LINK(Splice, &feed->singular) = nullptr;
    mutable_MISC(Pending, &feed->singular) = nullptr;

    return feed;
}

inline static void Free_Feed(REBFED *feed) {
    //
    // Aborting valist frames is done by just feeding all the values
    // through until the end.  This is assumed to do any work, such
    // as SINGULAR_FLAG_API_RELEASE, which might be needed on an item.  It
    // also ensures that va_end() is called, which happens when the frame
    // manages to feed to the end.
    //
    // Note: While on many platforms va_end() is a no-op, the C standard
    // is clear it must be called...it's undefined behavior to skip it:
    //
    // http://stackoverflow.com/a/32259710/211160

    // !!! Since we're not actually fetching things to run them, this is
    // overkill.  A lighter sweep of the va_list pointers that did just
    // enough work to handle rebR() releases, and va_end()ing the list
    // would be enough.  But for the moment, it's more important to keep
    // all the logic in one place than to make variadic interrupts
    // any faster...they're usually reified into an array anyway, so
    // the frame processing the array will take the other branch.

    while (NOT_END(feed->value))
        Fetch_Next_In_Feed(feed);

    assert(IS_END(feed->value));
    assert(FEED_PENDING(feed) == nullptr);

    // !!! See notes in Fetch_Next regarding the somewhat imperfect way in
    // which splices release their holds.  (We wait until Free_Feed() so that
    // `do code: [clear code]` doesn't drop the hold until the block frame
    // is actually fully dropped.)
    //
    if (GET_FEED_FLAG(feed, TOOK_HOLD)) {
        assert(GET_SERIES_INFO(FEED_ARRAY(feed), HOLD));
        CLEAR_SERIES_INFO(m_cast(REBARR*, FEED_ARRAY(feed)), HOLD);
        CLEAR_FEED_FLAG(feed, TOOK_HOLD);
    }

    Free_Node(FED_POOL, cast(REBNOD*, feed));
}


// It is more pleasant to have a uniform way of speaking of frames by pointer,
// so this macro sets that up for you, the same way DECLARE_LOCAL does.  The
// optimizer should eliminate the extra pointer.
//
// Just to simplify matters, the frame cell is set to a bit pattern the GC
// will accept.  It would need stack preparation anyway, and this simplifies
// the invariant so if a recycle happens before Eval_Core() gets to its
// body, it's always set to something.  Using an unreadable trash means we
// signal to users of the frame that they can't be assured of any particular
// value between evaluations; it's not cleared.
//

inline static void Prep_Array_Feed(
    REBFED *feed,
    option(const RELVAL*) first,
    const REBARR *array,
    REBLEN index,
    REBSPC *specifier,
    REBFLGS flags
){
    feed->flags.bits = flags;

    if (first) {
        feed->value = unwrap(first);
        assert(NOT_END(feed->value));
        Init_Any_Array_At_Core(
            FEED_SINGLE(feed), REB_BLOCK, array, index, specifier
        );
        assert(KIND3Q_BYTE_UNCHECKED(feed->value) != REB_0_END);
            // ^-- faster than NOT_END()
    }
    else {
        feed->value = ARR_AT(array, index);
        if (feed->value == ARR_TAIL(array))
            feed->value = END_CELL;
        Init_Any_Array_At_Core(
            FEED_SINGLE(feed), REB_BLOCK, array, index + 1, specifier
        );
    }

    // !!! The temp locking was not done on end positions, because the feed
    // is not advanced (and hence does not get to the "drop hold" point).
    // This could be an issue for splices, as they could be modified while
    // their time to run comes up to not be END anymore.  But if we put a
    // hold on conservatively, it won't be dropped by Free_Feed() time.
    //
    if (IS_END(feed->value) or GET_SERIES_INFO(array, HOLD))
        NOOP;  // already temp-locked
    else {
        SET_SERIES_INFO(m_cast(REBARR*, array), HOLD);
        SET_FEED_FLAG(feed, TOOK_HOLD);
    }

    feed->gotten = nullptr;
    if (IS_END(feed->value))
        assert(FEED_PENDING(feed) == nullptr);
    else
        assert(READABLE(feed->value));
}

#define DECLARE_ARRAY_FEED(name,array,index,specifier) \
    REBFED *name = Alloc_Feed(); \
    Prep_Array_Feed(name, \
        nullptr, (array), (index), (specifier), FEED_MASK_DEFAULT \
    );

inline static void Prep_Va_Feed(
    struct Reb_Feed *feed,
    const void *p,
    option(va_list*) vaptr,
    REBFLGS flags
){
    // We want to initialize with something that will give back SPECIFIED.
    // It must therefore be bindable.  Try a COMMA!
    //
    Init_Comma(FEED_SINGLE(feed));

    feed->flags.bits = flags;
    if (not vaptr) {  // `p` should be treated as a packed void* array
        FEED_VAPTR_POINTER(feed) = nullptr;
        FEED_PACKED(feed) = cast(const void* const*, p);
        p = *FEED_PACKED(feed)++;
    }
    else {
        FEED_VAPTR_POINTER(feed) = unwrap(vaptr);
        FEED_PACKED(feed) = nullptr;
    }
    Detect_Feed_Pointer_Maybe_Fetch(feed, p);

    feed->gotten = nullptr;
    assert(IS_END(feed->value) or READABLE(feed->value));
}

// The flags is passed in by the macro here by default, because it does a
// fetch as part of the initialization from the `first`...and if you want
// the flags to take effect, they must be passed in up front.
//
#define DECLARE_VA_FEED(name,p,vaptr,flags) \
    REBFED *name = Alloc_Feed(); \
    Prep_Va_Feed(name, (p), (vaptr), (flags)); \

inline static void Prep_Any_Array_Feed(
    REBFED *feed,
    const RELVAL *any_array,  // array is extracted and HOLD put on
    REBSPC *specifier,
    REBFLGS parent_flags  // only reads FEED_FLAG_CONST out of this
){
    // Note that `CELL_FLAG_CONST == FEED_FLAG_CONST`
    //
    REBFLGS flags;
    if (GET_CELL_FLAG(any_array, EXPLICITLY_MUTABLE))
        flags = FEED_MASK_DEFAULT;  // override const from parent frame
    else
        flags = FEED_MASK_DEFAULT
            | (parent_flags & FEED_FLAG_CONST)  // inherit
            | (any_array->header.bits & CELL_FLAG_CONST);  // heed

    Prep_Array_Feed(
        feed,
        nullptr,  // `first` = nullptr, don't inject arbitrary 1st element
        VAL_ARRAY(any_array),
        VAL_INDEX(any_array),
        Derive_Specifier(specifier, any_array),
        flags
    );
}

#define DECLARE_FEED_AT_CORE(name,any_array,specifier) \
    REBFED *name = Alloc_Feed(); \
    Prep_Any_Array_Feed(name, \
        (any_array), (specifier), FS_TOP->feed->flags.bits \
    );

#define DECLARE_FEED_AT(name,any_array) \
    DECLARE_FEED_AT_CORE(name, (any_array), SPECIFIED)
