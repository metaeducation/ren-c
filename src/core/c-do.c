//
//  File: %c-do.c
//  Summary: "DO Evaluator Wrappers"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// These are the "slightly more user-friendly" interfaces to the evaluator
// from %c-eval.c.  These routines will do the setup of the Reb_Frame state
// for you.
//
// Even "friendlier" interfaces are available as macros on top of these.
// See %sys-do.h for Do_Any_Array_At_Throws() and similar macros.
//

#include "sys-core.h"


//
//  Detect_Feed_Pointer_Maybe_Fetch: C
//
// Ordinary Rebol internals deal with REBVAL* that are resident in arrays.
// But a va_list can contain UTF-8 string components or special instructions
// that are other Detect_Rebol_Pointer() types.  Anyone who wants to set or
// preload a frame's state for a va_list has to do this detection, so this
// code has to be factored out to just take a void* (because a C va_list
// cannot have its first parameter in the variadic, va_list* is insufficient)
//
void Detect_Feed_Pointer_Maybe_Fetch(
    REBFED *feed,
    const void *p
){
    assert(FEED_PENDING(feed) == nullptr);

  detect_again:;

    // !!! On stack overflow errors, the system (theoretically) will go through
    // all the frames and make sure variadic feeds are ended.  If we put
    // trash in this value (e.g. 0xDECAFBAD) that code crashes.  For now, use
    // END so that if something below causes a stack overflow before the
    // operation finishes, those crashes don't happen.
    //
    feed->value = END_CELL;  // should be assigned below

    if (not p) {  // libRebol's null/<opt> (IS_NULLED prohibited in CELL case)

        // This is the compromise of convenience, where ~null~ is put in
        // to the feed.  If it's converted into an array we've told a
        // small lie (~null~ is a BAD-WORD! and a thing, so not the same
        // as the NULL non-thing).  It will evaluate to a ~null~ isotope
        // which *usually* acts like NULL, but not with ELSE/THEN directly.
        //
        // We must use something legal to put in arrays, so non-isotope.
        //
        Init_Bad_Word(&feed->fetched, Canon(NULL));

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
            feed
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

        // We are no longer deep binding code that is transcoded, instead the
        // idea is that the unbound state is a special signal to inherit more
        // liberally from the virtual binding "scope" chain.  With a variadic
        // we have to put this binding on the outermost nodes in the array
        // since there's no higher level value to poke the context into.
        //
        // !!! As a test we shallow bind the items to the context, then go
        // over the arrays and virtually bind them.
        //
        REBCTX *context = Get_Context_From_Stack();
        RELVAL *tail = ARR_TAIL(reified);
        RELVAL *item = ARR_HEAD(reified);

        // !!!  Complete rethink needed, but try to get it working first.  We
        // really need to know which cells were spliced and which were scanned,
        // and the best way to do that is bind as we go.

        for (; item != tail; ++item) {  // now virtual bind
            if (ANY_ARRAY(item)) {
                if (BINDING(item) == nullptr)
                    mutable_BINDING(item) = context;
            }
            else {
                DECLARE_LOCAL (temp);
                Derelativize(temp, item, SPC(context));
                Move_Cell(item, temp);
            }
        }

        feed->value = ARR_HEAD(reified);
        Init_Any_Array_At(FEED_SINGLE(feed), REB_BLOCK, reified, 1);
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
          case FLAVOR_INSTRUCTION_SPLICE: {
            REBVAL *single = SPECIFIC(ARR_SINGLE(inst1));
            if (IS_BLANK(single)) {
                GC_Kill_Series(inst1);
                goto detect_again;
            }

            if (IS_BLOCK(single)) {
                feed->value = nullptr;  // will become FEED_PENDING(), ignored
                Splice_Block_Into_Feed(feed, single);
            }
            else {
                assert(IS_QUOTED(single));
                Unquotify(Copy_Cell(&feed->fetched, single), 1);
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
            Copy_Cell(&feed->fetched, single);
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

        feed->value = cell;  // cell can be used as-is
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
        Init_Block(FEED_SINGLE(feed), EMPTY_ARRAY);
        break; }

      case DETECTED_AS_FREED_SERIES:
      case DETECTED_AS_FREED_CELL:
      default:
        panic (p);
    }
}


//
//  Fetch_Next_In_Feed: C
//
// Once a va_list is "fetched", it cannot be "un-fetched".  Hence only one
// unit of fetch is done at a time, into f->value.
//
void Fetch_Next_In_Feed(REBFED *feed) {
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
