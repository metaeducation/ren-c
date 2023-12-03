// %cell-series.h

// Uses "evil macro" variations because it is called so frequently, that in
// the debug build (which doesn't inline functions) there's a notable cost.
//
INLINE const Series* Cell_Series(NoQuote(const Cell*) v) {
    enum Reb_Kind heart = Cell_Heart(v);
    assert(Any_Series_Kind(heart) or heart == REB_URL);
    UNUSED(heart);
    const Series* s = c_cast(Series*, Cell_Node1(v));
    if (Get_Series_Flag(s, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return s;
}

#define Cell_Series_Ensure_Mutable(v) \
    m_cast(Series*, Cell_Series(Ensure_Mutable(v)))

#define Cell_Series_Known_Mutable(v) \
    m_cast(Series*, Cell_Series(Known_Mutable(v)))


// It is possible that the index could be to a point beyond the range of the
// series.  This is intrinsic, because the series data can be modified through
// one cell and not update the other cells referring to it.  Hence VAL_INDEX()
// must be checked, or the routine called with it must.

#define VAL_INDEX_RAW(v) \
    PAYLOAD(Any, (v)).second.i

#if defined(NDEBUG) || (! CPLUSPLUS_11)
    #define VAL_INDEX_UNBOUNDED(v) \
        VAL_INDEX_RAW(v)
#else
    // allows an assert, but uses C++ reference for lvalue:
    //
    //     VAL_INDEX_UNBOUNDED(v) = xxx;  // ensures v is Any_Series!
    //
    // Avoids READABLE() macro, because it's assumed that it was done in the
    // type checking to ensure VAL_INDEX() applied.  (This is called often.)
    //
    INLINE REBIDX VAL_INDEX_UNBOUNDED(NoQuote(const Cell*) v) {
        enum Reb_Kind k = Cell_Heart_Unchecked(v);  // only const if heart!
        assert(Any_Series_Kind(k));
        assert(Get_Cell_Flag_Unchecked(v, FIRST_IS_NODE));
        return VAL_INDEX_RAW(v);
    }
    INLINE REBIDX & VAL_INDEX_UNBOUNDED(Cell* v) {
        ASSERT_CELL_WRITABLE(v);
        enum Reb_Kind k = Cell_Heart_Unchecked(v);
        assert(Any_Series_Kind(k));
        assert(Get_Cell_Flag_Unchecked(v, FIRST_IS_NODE));
        return VAL_INDEX_RAW(v);  // returns a C++ reference
    }
#endif


INLINE REBLEN Cell_Series_Len_Head(NoQuote(const Cell*) v);  // forward decl

// Unlike VAL_INDEX_UNBOUNDED() that may give a negative number or past the
// end of series, VAL_INDEX() does bounds checking and always returns an
// unsigned REBLEN.
//
INLINE REBLEN VAL_INDEX(NoQuote(const Cell*) v) {
    enum Reb_Kind k = Cell_Heart(v);  // only const access if heart!
    assert(Any_Series_Kind(k));
    UNUSED(k);
    assert(Get_Cell_Flag(v, FIRST_IS_NODE));
    REBIDX i = VAL_INDEX_RAW(v);
    if (i < 0 or i > cast(REBIDX, Cell_Series_Len_Head(v)))
        fail (Error_Index_Out_Of_Range_Raw());
    return i;
}


INLINE void INIT_SPECIFIER(Cell* v, Stub* binding) {
    //
    // can be called on non-bindable series, but p must be nullptr

    BINDING(v) = binding;

  #if !defined(NDEBUG)
    if (not binding)
        return;  // e.g. UNBOUND

    assert(Is_Bindable(v));  // works on partially formed values

    assert(Is_Node_Managed(binding));
    assert(
        IS_DETAILS(binding)  // relative
        or IS_VARLIST(binding)  // specific
        or (
            Any_Array(v) and (IS_LET(binding) or IS_USE(binding)) // virtual
        ) or (
            Is_Varargs(v) and Not_Series_Flag(binding, DYNAMIC)
        )  // varargs from MAKE VARARGS! [...], else is a varlist
    );
  #endif
}


INLINE REBVAL *Init_Series_Cell_At_Core(
    Cell* out,
    enum Reb_Kind type,
    const Series* s,  // ensured managed by calling macro
    REBLEN index,
    Stub* specifier
){
  #if !defined(NDEBUG)
    assert(Any_Series_Kind(type) or type == REB_URL);
    assert(Is_Node_Managed(s));

    // Note: a R3-Alpha Make_Binary() comment said:
    //
    //     Make a binary string series. For byte, C, and UTF8 strings.
    //     Add 1 extra for terminator.
    //
    // One advantage of making all binaries terminate in 0 is that it means
    // that if they were valid UTF-8, they could be aliased as Rebol strings,
    // which are zero terminated.  So it's the rule.
    //
    Assert_Series_Term_If_Needed(s);

    if (Any_Array_Kind(type))
        assert(Is_Series_Array(s));
    else if (Any_String_Kind(type))
        assert(Is_Series_UTF8(s));
    else {
        // Note: Binaries are allowed to alias strings
    }
  #endif

    Reset_Unquoted_Header_Untracked(
        out,
        FLAG_HEART_BYTE(type) | CELL_FLAG_FIRST_IS_NODE
    );
    Init_Cell_Node1(out, s);
    VAL_INDEX_RAW(out) = index;
    INIT_SPECIFIER(out, specifier);  // asserts if unbindable type tries to bind
    return cast(REBVAL*, out);
}

#define Init_Series_Cell_At(v,t,s,i) \
    Init_Series_Cell_At_Core((v), (t), \
        Force_Series_Managed_Core(s), (i), UNBOUND)

#define Init_Series_Cell(v,t,s) \
    Init_Series_Cell_At((v), (t), (s), 0)
