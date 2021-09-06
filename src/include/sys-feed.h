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


#if !defined(__cplusplus)
    #define QUOTING_BYTE(feed) \
        mutable_SECOND_BYTE((feed)->flags.bits)
#else
    inline static REBYTE& QUOTING_BYTE(REBFED *feed)  // type checks feed...
        { return mutable_SECOND_BYTE(feed->flags.bits); }  // ...but mutable
#endif


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


// Ordinary Rebol internals deal with REBVAL* that are resident in arrays.
// But a va_list can contain UTF-8 string components or special instructions
// that are other Detect_Rebol_Pointer() types.  Anyone who wants to set or
// preload a frame's state for a va_list has to do this detection, so this
// code has to be factored out to just take a void* (because a C va_list
// cannot have its first parameter in the variadic, va_list* is insufficient)
//
inline static void Detect_Feed_Pointer_Maybe_Fetch(
    REBFED *feed,
    const void *p
){
    assert(FEED_PENDING(feed) == nullptr);

  detect_again:;

    TRASH_POINTER_IF_DEBUG(feed->value);  // should be assigned below

    if (not p) {  // libRebol's null/<opt> (IS_NULLED prohibited in CELL case)

        if (QUOTING_BYTE(feed) == 0) {

            // This is the compromise of convenience, where ~null~ is put in
            // to the feed.  If it's converted into an array we've told a
            // small lie (~null~ is a BAD-WORD! and a thing, so not the same
            // as the NULL non-thing).  It will evaluate to a ~null~ isotope
            // which *usually* acts like NULL, but not with ELSE/THEN directly.
            //
            // We must use something legal to put in arrays, so non-isotope.
            //
            Init_Bad_Word(RESET(&feed->fetched), Canon(NULL));
        }
        else {
            // !!! "We could make a global QUOTED_NULLED_VALUE with a stable
            // pointer and not have to use fetched." <- silly optimization

            Init_Nulled(RESET(&feed->fetched));
            Isotopic_Quotify(&feed->fetched, QUOTING_BYTE(feed));
        }

        assert(FEED_SPECIFIER(feed) == SPECIFIED);  // !!! why assert this?
        feed->value = &feed->fetched;

    } else switch (Detect_Rebol_Pointer(p)) {

      case DETECTED_AS_UTF8: {
        REBDSP dsp_orig = DSP;

        // Note that the context is only used on loaded text from C string
        // data.  The scanner leaves all spliced values with whatever bindings
        // they have (even if that is none).
        //
        // !!! Some kind of "binding instruction" might allow other uses?
        //
        SCAN_LEVEL level;
        SCAN_STATE ss;
        const REBLIN start_line = 1;
        Init_Va_Scan_Level_Core(
            &level,
            &ss,
            Intern_Unsized_Managed("-variadic-"),
            start_line,
            cast(const REBYTE*, p),
            feed,
            Get_Context_From_Stack()
        );

        REBVAL *error = rebRescue(cast(REBDNG*, &Scan_To_Stack), &level);

        if (error) {
            REBCTX *error_ctx = VAL_CONTEXT(error);
            rebRelease(error);
            fail (error_ctx);
        }

        if (DSP == dsp_orig) {
            //
            // This happens when somone says rebValue(..., "", ...) or similar,
            // and gets an empty array from a string scan.  It's not legal
            // to put an END in f->value, and it's unknown if the variadic
            // feed is actually over so as to put null... so get another
            // value out of the va_list and keep going.
            //
            if (FEED_VAPTR(feed))
                p = va_arg(*unwrap(FEED_VAPTR(feed)), const void*);
            else
                p = *FEED_PACKED(feed)++;
            goto detect_again;
        }

        // !!! for now, assume scan went to the end; ultimately it would need
        // to pass the feed in as a parameter for partial scans
        //
        assert(not FEED_IS_VARIADIC(feed));

        REBARR *reified = Pop_Stack_Values(dsp_orig);

        // !!! We really should be able to free this array without managing it
        // when we're done with it, though that can get a bit complicated if
        // there's an error or need to reify into a value.  For now, do the
        // inefficient thing and manage it.
        //
        // !!! Scans that produce only one value (which are likely very
        // common) can go into feed->fetched and not make an array at all.
        //
        Manage_Series(reified);

        feed->value = ARR_HEAD(reified);
        Init_Any_Array_At(RESET(FEED_SINGLE(feed)), REB_BLOCK, reified, 1);
        break; }

      case DETECTED_AS_SERIES: {  // e.g. rebQ, rebU, or a rebR() handle
        REBARR *inst1 = ARR(m_cast(void*, p));

        // As we feed forward, we're supposed to be freeing this--it is not
        // managed -and- it's not manuals tracked, it is only held alive by
        // the va_list()'s plan to visit it.  A fail() here won't auto free
        // it *because it is this traversal code which is supposed to free*.
        //
        // !!! Actually, THIS CODE CAN'T FAIL.  :-/  It is part of the
        // implementation of fail's cleanup itself.
        //
        switch (SER_FLAVOR(inst1)) {
          case FLAVOR_INSTRUCTION_ADJUST_QUOTING: {
            assert(NOT_SERIES_FLAG(inst1, MANAGED));

            // !!! Previously this didn't allow the case of:
            //
            //    QUOTING_BYTE(feed) + MISC(inst1).quoting_delta < 0
            //
            // Because it said rebU() "couldn't unquote a feed splicing plain
            // values".  However, there was a mechanical problem because it
            // was putting plain NULLs into the instruction array...and nulls
            // aren't valid in most arrays.  Rather than make an exception,
            // everything was quoted up one and the delta decremented.  See
            // rebQUOTING for this, which needs more design attention.

            assert(ARR_LEN(inst1) > 0);
            if (ARR_LEN(inst1) > 1)
                panic ("rebU() of more than one value splice not written");

            REBVAL *single = SPECIFIC(ARR_SINGLE(inst1));
            Overwrite_Cell(&feed->fetched, single);
            Isotopic_Quotify(
                &feed->fetched,
                QUOTING_BYTE(feed) + inst1->misc.quoting_delta
            );
            feed->value = &feed->fetched;

            GC_Kill_Series(inst1);  // not manuals-tracked
            break; }

          case FLAVOR_INSTRUCTION_SPLICE: {
            REBVAL *single = SPECIFIC(ARR_SINGLE(inst1));
            if (IS_BLOCK(single)) {
                feed->value = nullptr;  // will become FEED_PENDING(), ignored
                Splice_Block_Into_Feed(feed, single);
            }
            else {
                Copy_Cell(RESET(&feed->fetched), single);
                feed->value = &feed->fetched;
            }
            GC_Kill_Series(inst1);
            break; }

          case FLAVOR_API: {
            //
            // We usually get the API *cells* passed to us, not the singular
            // array holding them.  But the rebR() function will actually
            // flip the "release" flag and then return the existing API handle
            // back, now behaving as an instruction.
            //
            assert(GET_SUBCLASS_FLAG(API, inst1, RELEASE));

            // !!! Originally this asserted it was a managed handle, but the
            // needs of API-TRANSIENT are such that a handle which outlives
            // the frame is returned as a SINGULAR_API_RELEASE.  Review.
            //
            /*assert(GET_SERIES_FLAG(inst1, MANAGED));*/

            // See notes above (duplicate code, fix!) about how we might like
            // to use the as-is value and wait to free until the next cycle
            // vs. putting it in fetched/MARKED_TEMPORARY...but that makes
            // this more convoluted.  Review.

            REBVAL *single = SPECIFIC(ARR_SINGLE(inst1));
            Copy_Cell(RESET(&feed->fetched), single);
            Isotopic_Quotify(&feed->fetched, QUOTING_BYTE(feed));
            feed->value = &feed->fetched;
            rebRelease(single);  // *is* the instruction
            break; }

          default:
            //
            // Besides instructions, other series types aren't currenlty
            // supported...though it was considered that you could use
            // REBCTX* or REBACT* directly instead of their archtypes.  This
            // was considered when thinking about ditching value archetypes
            // altogether (e.g. no usable cell pattern guaranteed at the head)
            // but it's important in several APIs to emphasize a value gives
            // phase information, while archetypes do not.
            //
            panic (inst1);
        }
        break; }

      case DETECTED_AS_CELL: {
        const REBVAL *cell = cast(const REBVAL*, p);
        assert(not IS_RELATIVE(cast(const RELVAL*, cell)));

        assert(FEED_SPECIFIER(feed) == SPECIFIED);

        if (IS_NULLED(cell))  // API enforces use of C's nullptr (0) for NULL
            assert(!"NULLED cell API leak, see NULLIFY_NULLED() in C source");

        if (QUOTING_BYTE(feed) == 0) {
            feed->value = cell;  // cell can be used as-is
        }
        else {
            // We don't want to corrupt the value itself.  We have to move
            // it into the fetched cell and quote it.
            //
            Copy_Cell(&feed->fetched, cell);
            Isotopic_Quotify(&feed->fetched, QUOTING_BYTE(feed));
            feed->value = &feed->fetched;  // note END is detected separately
        }
        break; }

      case DETECTED_AS_END: {  // end of variadic input, so that's it for this
        feed->value = END_CELL;

        // The va_end() is taken care of here, or if there is a throw/fail it
        // is taken care of by Abort_Frame_Core()
        //
        if (FEED_VAPTR(feed))
            va_end(*unwrap(FEED_VAPTR(feed)));
        else
            assert(FEED_PACKED(feed));

        // !!! Error reporting expects there to be an array.  The whole story
        // of errors when there's a va_list is not told very well, and what
        // will have to likely happen is that in debug modes, all va_list
        // are reified from the beginning, else there's not going to be
        // a way to present errors in context.  Fake an empty array for now.
        //
        Init_Block(RESET(FEED_SINGLE(feed)), EMPTY_ARRAY);
        break; }

      case DETECTED_AS_FREED_SERIES:
      case DETECTED_AS_FREED_CELL:
      default:
        panic (p);
    }
}


//
// Fetch_Next_In_Feed()
//
// Once a va_list is "fetched", it cannot be "un-fetched".  Hence only one
// unit of fetch is done at a time, into f->value.
//
inline static void Fetch_Next_In_Feed(REBFED *feed) {
    //
    // !!! This used to assert that feed->value wasn't "IS_END()".  Things have
    // gotten more complex, because feed->fetched may have been Move_Cell()'d
    // from, which triggers a RESET() and that's indistinguishable from END.
    // To the extent the original assert provided safety, revisit it.

    // The NEXT_ARG_FROM_OUT flag is a trick used by frames, which must be
    // careful about the management of the trick.  It's put on the feed
    // and not the frame in order to catch cases where it slips by, so this
    // assert is important.
    //
    if (GET_FEED_FLAG(feed, NEXT_ARG_FROM_OUT))
        assert(!"Fetch_Next_In_Feed() called but NEXT_ARG_FROM_OUT set");

    // We are changing ->value, and thus by definition any ->gotten value
    // will be invalid.  It might be "wasteful" to always set this to null,
    // especially if it's going to be overwritten with the real fetch...but
    // at a source level, having every call to Fetch_Next_In_Frame have to
    // explicitly set ->gotten to null is overkill.  Could be split into
    // a version that just trashes ->gotten in the debug build vs. null.
    //
    feed->gotten = nullptr;

  retry_splice:
    if (FEED_PENDING(feed)) {
        assert(NOT_END(FEED_PENDING(feed)));

        feed->value = FEED_PENDING(feed);
        mutable_MISC(Pending, &feed->singular) = nullptr;
    }
    else if (FEED_IS_VARIADIC(feed)) {
        //
        // A variadic can source arbitrary pointers, which can be detected
        // and handled in different ways.  Notably, a UTF-8 string can be
        // differentiated and loaded.
        //
        if (FEED_VAPTR(feed)) {
            const void *p = va_arg(*unwrap(FEED_VAPTR(feed)), const void*);
            Detect_Feed_Pointer_Maybe_Fetch(feed, p);
        }
        else {
            //
            // C++ variadics use an ordinary packed array of pointers, because
            // they do more ambitious things with the arguments and there is
            // no (standard) way to construct a C va_list programmatically.
            //
            const void *p = *FEED_PACKED(feed)++;
            Detect_Feed_Pointer_Maybe_Fetch(feed, p);
        }
    }
    else {
        if (FEED_INDEX(feed) != cast(REBINT, ARR_LEN(FEED_ARRAY(feed)))) {
            feed->value = ARR_AT(FEED_ARRAY(feed), FEED_INDEX(feed));
            ++FEED_INDEX(feed);
        }
        else {
            feed->value = END_CELL;

            // !!! At first this dropped the hold here; but that created
            // problems if you write `do code: [clear code]`, because END
            // is reached when CODE is fulfilled as an argument to CLEAR but
            // before CLEAR runs.  This subverted the series hold mechanic.
            // Instead we do the drop in Free_Feed(), though drops on splices
            // happen here.  It's not perfect, but holds need systemic review.

            if (FEED_SPLICE(feed)) {  // one or more additional splices to go
                if (GET_FEED_FLAG(feed, TOOK_HOLD)) {  // see note above
                    assert(GET_SERIES_INFO(FEED_ARRAY(feed), HOLD));
                    CLEAR_SERIES_INFO(m_cast(REBARR*, FEED_ARRAY(feed)), HOLD);
                    CLEAR_FEED_FLAG(feed, TOOK_HOLD);
                }

                REBARR *splice = FEED_SPLICE(feed);
                memcpy(FEED_SINGULAR(feed), FEED_SPLICE(feed), sizeof(REBARR));
                GC_Kill_Series(splice);
                goto retry_splice;
            }
        }
    }
}


// Most calls to Fetch_Next_In_Frame() are no longer interested in the
// cell backing the pointer that used to be in f->value (this is enforced
// by a rigorous test in DEBUG_EXPIRED_LOOKBACK).  Special care must be
// taken when one is interested in that data, because it may have to be
// moved.  So current can be returned from Fetch_Next_In_Frame_Core().

inline static const RELVAL *Lookback_While_Fetching_Next(REBFRM *f) {
  #ifdef DEBUG_EXPIRED_LOOKBACK
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
            RESET(&f->feed->lookback),
            SPECIFIC(&f->feed->fetched),
            CELL_MASK_ALL
        );
        lookback = &f->feed->lookback;
    }
    else
        lookback = f->feed->value;

    Fetch_Next_In_Feed(f->feed);

  #ifdef DEBUG_EXPIRED_LOOKBACK
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
  #ifdef DEBUG_COUNT_TICKS
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
        assert(NOT_END(first));
        feed->value = unwrap(first);
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
// FLAG_QUOTING_BYTE() to take effect, it must be passed in up front.
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
