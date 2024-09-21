//
//  File: %sys-feed.h
//  Summary: {Accessors and Argument Pushers/Poppers for Function Call Levels}
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
// (feed->p), and to be able to have another pointer to the previous
// value one must request a "lookback" at the time of advancing the feed.
//
// One reason for the feed's strict nature is that it offers an interface not
// just to Rebol BLOCK!s and other lists, but also to variadic lists such
// as C's va_list...in a system which also allows the mixure of portions of
// UTF-8 string source text.  C's va_list does not retain a memory of the
// past, so once va_arg() is called it forgets the previous value...and
// since values may also be fabricated from text it can get complicated.
//
// Another reason for the strictness is to help rein in the evaluator design
// to keep it within... a... "certain boundary of complexity".  :-P
//


//=//// VARIADIC FEED END SIGNAL //////////////////////////////////////////=//
//
// The API uses C's 0-valued nullptr for the purpose of representing null
// value handles.  So `rebValue("any [", value, "10]", nullptr)` can't be used
// to signal the end of input.  We use instead a pointer to a 2-byte sequence
// that's easy to create on a local stack in C and WebAssembly: the 2-bytes
// of 192 followed by 0.  The C string literal "\xC0" creates it, and is
// defined as rebEND...which is automatically added to the tail of calls to
// things like rebValue via a variadic macro.  (See rebEND for more info.)

#if (! DEBUG_CHECK_ENDS)
    #define Is_End(p) \
        (((const Byte*)(p))[0] == END_SIGNAL_BYTE)  // Note: needs (p) parens!
#else
    template<typename T>  // should only test const void* for ends
    INLINE bool Is_End(T* p) {
        static_assert(
            std::is_same<T, const void>::value,
            "Is_End() is not designed to operate on Cell, Flex, etc."
        );
        const Byte* bp = c_cast(Byte*, p);
        if (*bp != END_SIGNAL_BYTE) {
            assert(*bp & NODE_BYTEMASK_0x01_CELL);
            return false;
        }
        assert(bp[1] == 0);  // not strictly necessary, but rebEND is 2 bytes
        return true;
    }
#endif

#define Not_End(p) \
    (not Is_End(p))


#define FEED_SINGULAR(feed)     (&(feed)->singular)
#define FEED_SINGLE(feed)       Stub_Cell(&(feed)->singular)

#define LINK_Splice_TYPE        Stub*
#define HAS_LINK_Splice         FLAVOR_FEED

#define MISC_Pending_TYPE       const Cell*
#define HAS_MISC_Pending        FLAVOR_FEED


// Nullptr is used by the API to indicate null cells.  We want the frequent
// tests for being at the end of a feed to not require a dereference, which
// Is_End() does (because rebEND is a string literal that can be instantiated
// at many different addresses, we have to dereference the pointer to check it)
//
// Instead we use a global pointer (could also be a "magic number", possibly
// would check faster).
//
#define Is_Feed_At_End(feed) \
    ((feed)->p == &PG_Feed_At_End)

#define Not_Feed_At_End(feed) \
    (not Is_Feed_At_End(feed))

#define FEED_SPLICE(feed) \
    LINK(Splice, &(feed)->singular)

// This contains a nullptr if the next fetch should be an attempt
// to consult the va_list (if any).
//
#define FEED_PENDING(feed) \
    MISC(Pending, &(feed)->singular)

#define FEED_IS_VARIADIC(feed) \
    (REB_COMMA == HEART_BYTE(FEED_SINGLE(feed)))

#define FEED_VAPTR_POINTER(feed)    PAYLOAD(Comma, FEED_SINGLE(feed)).vaptr
#define FEED_PACKED(feed)           PAYLOAD(Comma, FEED_SINGLE(feed)).packed

INLINE const Element* At_Feed(Feed* feed) {
    assert(Not_Feed_Flag(feed, NEEDS_SYNC));
    assert(feed->p != &PG_Feed_At_End);

    const Element* elem = c_cast(Element*, feed->p);
    if (Get_Cell_Flag(elem, FEED_NOTE_META)) {
        DECLARE_VALUE (temp);
        Copy_Cell(temp, elem);
        Meta_Unquotify_Known_Stable(temp);
        fail (Error_Bad_Antiform(temp));
    }
    return elem;
}

INLINE const Element* Try_At_Feed(Feed* feed) {
    assert(Not_Feed_Flag(feed, NEEDS_SYNC));
    if (feed->p == &PG_Feed_At_End)
        return nullptr;

    return At_Feed(feed);
}

INLINE Option(va_list*) FEED_VAPTR(Feed* feed) {
    assert(FEED_IS_VARIADIC(feed));
    return FEED_VAPTR_POINTER(feed);
}



// For performance, we always get the specifier from the same location, even
// if we're not using an array.  So for the moment, that means using a
// COMMA! (which for technical reasons has a nullptr binding and is thus
// always SPECIFIED).  However, Cell_Specifier() only runs on arrays, so
// we sneak past that by accessing the node directly.
//
#define FEED_SPECIFIER(feed) \
    cast(Array*, BINDING(FEED_SINGLE(feed)))

#define mutable_FEED_SPECIFIER(feed) \
    BINDING(FEED_SINGLE(feed))

#define FEED_ARRAY(feed) \
    Cell_Array(FEED_SINGLE(feed))

#define FEED_INDEX(feed) \
    VAL_INDEX_UNBOUNDED(FEED_SINGLE(feed))


// 1. The va_end() is taken care of here; all code--regardless of throw or
//    errors--must walk through feeds to the end in order to clean up manual
//    Flexes backing instructions (and also to run va_end() if needed, which
//    is required by the standard and may be essential on some platforms).
//
// 2. !!! Error reporting expects there to be an array.  The whole story of
//    errors when there's a va_list is not told very well, and what will
//    have to likely happen is that in debug modes, all va_list are reified
//    from the beginning, else there's not going to be a way to present
//    errors in context.  Fake an empty array for now.
//
INLINE void Finalize_Variadic_Feed(Feed* feed) {
    assert(FEED_IS_VARIADIC(feed));
    assert(FEED_PENDING(feed) == nullptr);

    assert(Is_Feed_At_End(feed));  // must spool, regardless of throw/fail!

    if (FEED_VAPTR(feed))
        va_end(*(unwrap FEED_VAPTR(feed)));  // *ALL* valist get here [1]
    else
        assert(FEED_PACKED(feed));

    Corrupt_Pointer_If_Debug(FEED_VAPTR_POINTER(feed));
    Corrupt_Pointer_If_Debug(FEED_PACKED(feed));
}


// Function used by the scanning machinery when transforming a pointer from
// the variadic API feed (the pointer already identified as a cell).
//
// 1. The API enforces use of C's nullptr (0) as the signal for ~null~
//    antiforms.  (That's handled by a branch that skips this routine.)
//    But internally, cells have an antiform WORD! payload for this case,
//    and those internal cells are legal to pass to the API.
//
// 2. Various mechanics rely on the array feed being a "generic array", that
//    can be put into a REB_BLOCK.  This means it cannot hold antiforms
//    (or voids).  But we want to hold antiforms and voids in suspended
//    animation in case there is an @ operator in the feed that will turn
//    them back into those forms.  So in those cases, meta it and set a
//    cell flag to notify the At_Feed() machinery about the strange case
//    (it will error, the @ code in the evaluator uses a different function).
//
INLINE const Element* Copy_Reified_Variadic_Feed_Cell(
    Sink(Element*) out,
    const Cell* cell
){
    if (Is_Nulled(cell))
        assert(not Is_Api_Value(cell));  // only internals can be nulled [1]

    if (QUOTE_BYTE(cell) == ANTIFORM_0) {
        Copy_Meta_Cell(out, cell);
        Set_Cell_Flag(out, FEED_NOTE_META);  // @ turns back [2]
    }
    else
        Copy_Cell(out, c_cast(Element*, cell));

    return out;
}


// As we feed forward, we're supposed to be freeing this--it is not managed
// -and- it's not manuals tracked, it is only held alive by the va_list()'s
// plan to visit it.  A fail() here won't auto free it *because it is this
// traversal code which is supposed to free*.
//
// !!! Actually, THIS CODE CAN'T FAIL.  :-/  It is part of the implementation
// of fail's cleanup itself.
//
INLINE Option(const Element*) Try_Reify_Variadic_Feed_At(
    Feed* feed
){
    const Flex* f = c_cast(Flex*, feed->p);

    switch (Flex_Flavor(f)) {
      case FLAVOR_INSTRUCTION_SPLICE: {
        Array* inst1 = x_cast(Array*, f);
        Element* single = cast(Element*, Stub_Cell(inst1));
        if (Is_Blank(single)) {
            GC_Kill_Flex(inst1);
            return nullptr;
        }

        if (Is_Block(single)) {
            feed->p = &PG_Feed_At_End;  // will become FEED_PENDING(), ignored
            Splice_Block_Into_Feed(feed, single);
        }
        else {
            assert(Is_Quoted(single));
            Unquotify(Copy_Cell(&feed->fetched, single), 1);
            feed->p = &feed->fetched;
        }
        GC_Kill_Flex(inst1);
        break; }

      case FLAVOR_API: {
        Array* inst1 = x_cast(Array*, f);

        // We usually get the API *cells* passed to us, not the singular
        // array holding them.  But the rebR() function will actually
        // flip the "release" flag and then return the existing API handle
        // back, now behaving as an instruction.
        //
        assert(Get_Subclass_Flag(API, inst1, RELEASE));

        // !!! Originally this asserted it was a managed handle, but the
        // needs of API-TRANSIENT are such that a handle which outlives
        // the level is returned as a SINGULAR_API_RELEASE.  Review.
        //
        /*assert(Is_Node_Managed(inst1));*/

        // See notes above (duplicate code, fix!) about how we might like
        // to use the as-is value and wait to free until the next cycle
        // vs. putting it in fetched/MARKED_TEMPORARY...but that makes
        // this more convoluted.  Review.

        Value* single = Stub_Cell(inst1);
        feed->p = single;
        feed->p = Copy_Reified_Variadic_Feed_Cell(
            &feed->fetched,
            c_cast(Cell*, feed->p)
        );
        rebRelease(single);  // *is* the instruction
        break; }

        // This lets you use a symbol and it assumes you want a WORD!.  If all
        // you have is an antiform ACTION! available, this means Canon(WORD)
        // can be cheaper than rebM(Lib(WORD)) for the action, especially if
        // the ->gotten field is set up.  Using words can also be more clear
        // in debugging than putting the actions themselves.
        //
      case FLAVOR_SYMBOL: {
        Init_Any_Word(&feed->fetched, REB_WORD, c_cast(Symbol*, f));
        feed->p = &feed->fetched;
        break; }

      default:
        //
        // Besides instructions, other series types aren't currenlty
        // supported...though it was considered that you could use
        // Context* or Action* directly instead of their archetypes.  This
        // was considered when thinking about ditching value archetypes
        // altogether (e.g. no usable cell pattern guaranteed at the head)
        // but it's important in several APIs to emphasize a value gives
        // phase information, while archetypes do not.
        //
        panic (feed->p);
    }

    return cast(const Element*, feed->p);
}


// Ordinary Rebol internals deal with Value* that are resident in arrays.
// But a va_list can contain UTF-8 string components or special instructions
//
INLINE void Force_Variadic_Feed_At_Cell_Or_End_May_Fail(Feed* feed)
{
    assert(FEED_IS_VARIADIC(feed));
    assert(FEED_PENDING(feed) == nullptr);

  detect: {  /////////////////////////////////////////////////////////////////

  // 1. This happens when an empty array comes from a string scan.  It's not
  //    legal to put an END in L->value unless the array is actually over, so
  //    get another pointer out of the va_list and keep going.

    if (not feed->p) {  // libRebol's NULL (prohibited as an Is_Nulled() CELL)

        feed->p = Root_Feed_Null_Substitute;

    } else switch (Detect_Rebol_Pointer(feed->p)) {

      case DETECTED_AS_END:  // end of input (all feeds must be spooled to end)
        feed->p = &PG_Feed_At_End;
        break;  // va_end() handled by Free_Feed() logic

      case DETECTED_AS_CELL:
        break;

      case DETECTED_AS_STUB:  // e.g. rebQ, rebU, or a rebR() handle
        if (not Try_Reify_Variadic_Feed_At(feed))
            goto detect_again;
        break;

      case DETECTED_AS_UTF8: {
        // !!! Some kind of "binding instruction" might allow other uses?
        //
        // !!! We really should be able to free this array without managing it
        // when we're done with it, though that can get a bit complicated if
        // there's an error or need to reify into a value.  For now, do the
        // inefficient thing and manage it.
        //
        // !!! Scans that produce only one value (which are likely very
        // common) can go into feed->fetched and not make an array at all.
        //
        Specifier* specifier = FEED_SPECIFIER(feed);
        Array* reified = maybe Try_Scan_Variadic_Feed_Utf8_Managed(feed);

        if (not reified) {  // rebValue("", ...) [1]
            if (Is_Feed_At_End(feed))
                break;
            goto detect_again;
        }

        // !!! for now, assume scan went to the end; ultimately it would need
        // to pass the feed in as a parameter for partial scans
        //
        assert(Is_Feed_At_End(feed));
        Finalize_Variadic_Feed(feed);

        feed->p = Array_Head(reified);
        Init_Any_List_At(FEED_SINGLE(feed), REB_BLOCK, reified, 1);
        mutable_FEED_SPECIFIER(feed) = specifier;
        break; }

      default:
        panic (feed->p);
    }

    assert(Is_Feed_At_End(feed) or Ensure_Readable(c_cast(Cell*, feed->p)));
    return;

} detect_again: {  ///////////////////////////////////////////////////////////

    if (FEED_VAPTR(feed))
        feed->p = va_arg(*(unwrap FEED_VAPTR(feed)), const void*);
    else
        feed->p = *FEED_PACKED(feed)++;

    goto detect;
}}


// This is higher-level, and should be called by non-internal feed mechanics.
//
INLINE void Sync_Feed_At_Cell_Or_End_May_Fail(Feed* feed) {
    if (Get_Feed_Flag(feed, NEEDS_SYNC)) {
        Force_Variadic_Feed_At_Cell_Or_End_May_Fail(feed);
        Clear_Feed_Flag(feed, NEEDS_SYNC);
    }
    assert(Is_Feed_At_End(feed) or Ensure_Readable(c_cast(Cell*, feed->p)));
}


//
// Fetch_Next_In_Feed()
//
// Once a va_list is "fetched", it cannot be "un-fetched".  Hence only one
// unit of fetch is done at a time, into L->value.
//
INLINE void Fetch_Next_In_Feed(Feed* feed) {
    assert(Not_Feed_Flag(feed, NEEDS_SYNC));

  #if DEBUG_PROTECT_FEED_CELLS
    if (not Is_Cell_Erased(&feed->fetched))
        Clear_Cell_Flag(&feed->fetched, PROTECTED);  // temp unprotect
  #endif

    assert(Not_End(feed->p));  // should test for end before fetching again
    Corrupt_Pointer_If_Debug(feed->p);

    // We are changing "Feed_At()", and thus by definition any ->gotten value
    // will be invalid.  It might be "wasteful" to always set this to null,
    // especially if it's going to be overwritten with the real fetch...but
    // at a source level, having every call to Fetch_Next_In_Feed have to
    // explicitly set ->gotten to null is overkill.  Could be split into
    // a version that just corrupts ->gotten in the debug build vs. null.
    //
    feed->gotten = nullptr;

  retry_splice:
    if (FEED_PENDING(feed)) {
        assert(FEED_PENDING(feed) != nullptr);

        feed->p = FEED_PENDING(feed);
        MISC(Pending, &feed->singular) = nullptr;
    }
    else if (FEED_IS_VARIADIC(feed)) {
        //
        // A variadic can source arbitrary pointers, which can be detected
        // and handled in different ways.  Notably, a UTF-8 string can be
        // differentiated and loaded.
        //
        if (FEED_VAPTR(feed)) {
            feed->p = va_arg(*(unwrap FEED_VAPTR(feed)), const void*);
        }
        else {
            // C++ variadics use an ordinary packed array of pointers, because
            // they do more ambitious things with the arguments and there is
            // no (standard) way to construct a C va_list programmatically.
            //
            feed->p = *FEED_PACKED(feed)++;
        }
        Force_Variadic_Feed_At_Cell_Or_End_May_Fail(feed);
    }
    else {
        if (FEED_INDEX(feed) != cast(REBINT, Array_Len(FEED_ARRAY(feed)))) {
            feed->p = Array_At(FEED_ARRAY(feed), FEED_INDEX(feed));
            ++FEED_INDEX(feed);
        }
        else {
            feed->p = &PG_Feed_At_End;

            // !!! At first this dropped the hold here; but that created
            // problems if you write `eval code: [clear code]`, because END
            // is reached when CODE is fulfilled as an argument to CLEAR but
            // before CLEAR runs.  This subverted the Flex hold mechanic.
            // Instead we do the drop in Free_Feed(), though drops on splices
            // happen here.  It's not perfect, but holds need systemic review.
            //
            if (FEED_SPLICE(feed)) {  // one or more additional splices to go
                if (Get_Feed_Flag(feed, TOOK_HOLD)) {  // see note above
                    assert(Get_Flex_Info(FEED_ARRAY(feed), HOLD));
                    Clear_Flex_Info(FEED_ARRAY(feed), HOLD);
                    Clear_Feed_Flag(feed, TOOK_HOLD);
                }

                Stub* splice = FEED_SPLICE(feed);
                Mem_Copy(
                    FEED_SINGULAR(feed),
                    FEED_SPLICE(feed),
                    sizeof(Stub)
                );
                GC_Kill_Flex(splice);  // Array* would hold reference
                goto retry_splice;
            }
        }
    }

  #if DEBUG_PROTECT_FEED_CELLS
    if (Not_Feed_At_End(feed) and not Is_Cell_Erased(&feed->fetched))
        Set_Cell_Flag(&feed->fetched, PROTECTED);
  #endif

    assert(Is_Feed_At_End(feed) or Ensure_Readable(c_cast(Cell*, feed->p)));
}


// This code is shared by The_Next_In_Feed(), and used without a feed
// advancement in the inert branch of the evaluator.  So for something like
// `repeat 2 [append [] 10]`, the steps are:
//
//    1. REPEAT defines its body parameter as <const>
//
//    2. When REPEAT runs Eval_Any_List_At_Throws() on the const ARG(body), the
//       feed gets FEED_FLAG_CONST due to the CELL_FLAG_CONST.
//
//    3. The argument to append is handled by the inert processing branch
//       which moves the value here.  If the block wasn't made explicitly
//       mutable (e.g. with MUTABLE) it takes the flag from the feed.
//
INLINE void Inertly_Derelativize_Inheriting_Const(
    Sink(Element*) out,
    const Element* v,
    Feed* feed
){
    Derelativize(out, v, FEED_SPECIFIER(feed));

    if (Not_Cell_Flag(v, EXPLICITLY_MUTABLE))
        out->header.bits |= (feed->flags.bits & FEED_FLAG_CONST);
}

INLINE void The_Next_In_Feed(Sink(Element*) out, Feed* feed) {
    Inertly_Derelativize_Inheriting_Const(out, At_Feed(feed), feed);
    Fetch_Next_In_Feed(feed);
}

INLINE void Just_Next_In_Feed(Sink(Element*) out, Feed* feed) {
    Copy_Cell(out, At_Feed(feed));
    if (Not_Cell_Flag(At_Feed(feed), EXPLICITLY_MUTABLE))
        out->header.bits |= (feed->flags.bits & FEED_FLAG_CONST);
    Fetch_Next_In_Feed(feed);
}


#define Alloc_Feed() \
    Alloc_Pooled(FEED_POOL)

INLINE void Free_Feed(Feed* feed) {
    //
    // Aborting valist feeds is done by just feeding all the values
    // through until the end.  This is assumed to do any work, such
    // as SINGULAR_FLAG_API_RELEASE, which might be needed on an item.  It
    // also ensures that va_end() is called, which happens when the feed
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
    // the level processing the array will take the other branch.

    Sync_Feed_At_Cell_Or_End_May_Fail(feed);  // may not be sync'd yet
    while (Not_Feed_At_End(feed))
        Fetch_Next_In_Feed(feed);

    assert(FEED_PENDING(feed) == nullptr);

    // !!! See notes in Fetch_Next regarding the somewhat imperfect way in
    // which splices release their holds.  (We wait until Free_Feed() so that
    // `eval code: [clear code]` doesn't drop the hold until the block level
    // is actually fully dropped.)
    //
    if (FEED_IS_VARIADIC(feed)) {
        Finalize_Variadic_Feed(feed);
    }
    else if (Get_Feed_Flag(feed, TOOK_HOLD)) {
        assert(Get_Flex_Info(FEED_ARRAY(feed), HOLD));
        Clear_Flex_Info(FEED_ARRAY(feed), HOLD);
        Clear_Feed_Flag(feed, TOOK_HOLD);
    }

    Free_Pooled(FEED_POOL, feed);
}

INLINE void Release_Feed(Feed* feed) {
    assert(feed->refcount > 0);
    if (--feed->refcount == 0)
        Free_Feed(feed);
}

INLINE Feed* Add_Feed_Reference(Feed* feed) {
    ++feed->refcount;
    return feed;
}

INLINE Feed* Prep_Feed_Common(void* preallocated, Flags flags) {
   Feed* feed = u_cast(Feed*, preallocated);

  #if DEBUG_COUNT_TICKS
    feed->tick = TG_tick;
  #endif

    Erase_Cell(&feed->fetched);

    Stub* s = Prep_Stub(
        &feed->singular,  // preallocated
        NODE_FLAG_NODE | FLAG_FLAVOR(FEED)
    );
    Erase_Cell(FEED_SINGLE(feed));
    LINK(Splice, s) = nullptr;
    MISC(Pending, s) = nullptr;

    feed->flags.bits = flags;
    Corrupt_Pointer_If_Debug(feed->p);
    Corrupt_Pointer_If_Debug(feed->gotten);

    feed->refcount = 0;  // putting in levels should add references

    return feed;
}

INLINE Feed* Prep_Array_Feed(
    void* preallocated,
    Option(const Cell*) first,
    const Array* array,
    REBLEN index,
    Specifier* specifier,
    Flags flags
){
    assert(not specifier or Is_Node_Managed(specifier));

    Feed* feed = Prep_Feed_Common(preallocated, flags);

    if (first) {
        feed->p = unwrap first;
        Init_Any_List_At_Core(
            FEED_SINGLE(feed), REB_BLOCK, array, index, specifier
        );
    }
    else {
        feed->p = Array_At(array, index);
        if (feed->p == Array_Tail(array))
            feed->p = &PG_Feed_At_End;
        Init_Any_List_At_Core(
            FEED_SINGLE(feed), REB_BLOCK, array, index + 1, specifier
        );
    }

    // !!! The temp locking was not done on end positions, because the feed
    // is not advanced (and hence does not get to the "drop hold" point).
    // This could be an issue for splices, as they could be modified while
    // their time to run comes up to not be END anymore.  But if we put a
    // hold on conservatively, it won't be dropped by Free_Feed() time.
    //
    if (Is_Feed_At_End(feed) or Get_Flex_Info(array, HOLD))
        NOOP;  // already temp-locked
    else {
        Set_Flex_Info(array, HOLD);
        Set_Feed_Flag(feed, TOOK_HOLD);
    }

    feed->gotten = nullptr;
    if (Is_Feed_At_End(feed))
        assert(FEED_PENDING(feed) == nullptr);
    else
        assert(Ensure_Readable(c_cast(Cell*, feed->p)));

    return feed;
}

#define Make_Array_Feed_Core(array,index,specifier) \
    Prep_Array_Feed( \
        Alloc_Feed(), \
        nullptr, (array), (index), (specifier), \
        FEED_MASK_DEFAULT \
    )


// Note: The invariant of a feed is that it must be cued up to having a ->value
// field set before the first Fetch_Next() is called.  So variadics lead to
// an awkward situation since they start off with a `p` pointer that needs
// to be saved somewhere that *isn't* a value.
//
// The way of dealing with this historically was to "prefetch" and kick-off
// the scanner before returning from Prep_Variadic_Feed().  So the entire
// scan could be finished in one swoop, transforming the va_list feed into
// an array form.
//
// This has some wide ramifications, such as meaning that scan errors will
// be triggered in the prep process...before the trampoline is running in
// effect with the guarding.  So that's bad.  It needs to stop.  But how?
//
// Note that the context is only used on loaded text from C string data.  The
// scanner leaves all spliced values with whatever bindings they have (even
// if that is none).
//
INLINE Feed* Prep_Variadic_Feed(
    void* preallocated,
    const void *p,
    Option(va_list*) vaptr,
    Flags flags
){
    Feed* feed = Prep_Feed_Common(preallocated, flags | FEED_FLAG_NEEDS_SYNC);

    // We want to initialize with something that will give back SPECIFIED.
    // It must therefore be bindable.  Try a COMMA!
    //
    Init_Comma(FEED_SINGLE(feed));

    if (not vaptr) {  // `p` should be treated as a packed void* array
        FEED_VAPTR_POINTER(feed) = nullptr;
        FEED_PACKED(feed) = cast(const void* const*, p);  // can't use c_cast()
        feed->p = *FEED_PACKED(feed)++;
    }
    else {
        FEED_VAPTR_POINTER(feed) = unwrap vaptr;
        FEED_PACKED(feed) = nullptr;
        feed->p = p;
    }

    // Note: We DON'T call Force_Variadic_Feed_At_Cell_Or_End_May_Fail() here.
    // Because we do not want Prep_Variadic_Feed() to fail, as it could have
    // no error trapping in effect...because it happens when levels are being
    // set up and haven't been pushed to the trampoline yet.
    //
    // The upshot of this is that if feed->p is a pointer to UTF8 or an
    // "instruction", it must be synchronized before you get a cell pointer.
    // So At_Feed() will assert if you do not synchronize first.

    feed->gotten = nullptr;

    return feed;
}

// The flags are passed in by the macro here by default, because it does a
// fetch as part of the initialization from the `first`...and if you want
// the flags to take effect, they must be passed in up front.
//
#define Make_Variadic_Feed(p,vaptr,flags) \
    Prep_Variadic_Feed(Alloc_Feed(), (p), (vaptr), (flags))

INLINE Feed* Prep_At_Feed(
    void *preallocated,
    const Cell* list,  // array is extracted and HOLD put on
    Specifier* specifier,
    Flags parent_flags  // only reads FEED_FLAG_CONST out of this
){
    STATIC_ASSERT(CELL_FLAG_CONST == FEED_FLAG_CONST);

    Flags flags;
    if (Get_Cell_Flag(list, EXPLICITLY_MUTABLE))
        flags = FEED_MASK_DEFAULT;  // override const from parent level
    else
        flags = FEED_MASK_DEFAULT
            | (parent_flags & FEED_FLAG_CONST)  // inherit
            | (list->header.bits & CELL_FLAG_CONST);  // heed

    return Prep_Array_Feed(
        preallocated,
        nullptr,  // `first` = nullptr, don't inject arbitrary 1st element
        Cell_Array(list),
        VAL_INDEX(list),
        Derive_Specifier(specifier, list),
        flags
    );
}

#define Make_At_Feed_Core(list,specifier) \
    Prep_At_Feed( \
        Alloc_Feed(), \
        (list), (specifier), TOP_LEVEL->feed->flags.bits \
    );

#define Make_At_Feed(name,list) \
    Make_At_Feed_Core(name, (list), SPECIFIED)
