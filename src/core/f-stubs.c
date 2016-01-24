/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  f-stubs.c
**  Summary: miscellaneous little functions
**  Section: functional
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"
#include "sys-deci-funcs.h"

//
//  REBCNT_To_Bytes: C
//
void REBCNT_To_Bytes(REBYTE *out, REBCNT in)
{
    assert(sizeof(REBCNT) == 4);
    out[0] = (REBYTE) in;
    out[1] = (REBYTE)(in >> 8);
    out[2] = (REBYTE)(in >> 16);
    out[3] = (REBYTE)(in >> 24);
}


//
//  Bytes_To_REBCNT: C
//
REBCNT Bytes_To_REBCNT(const REBYTE * const in)
{
    assert(sizeof(REBCNT) == 4);
    return (REBCNT) in[0]          // & 0xFF
        | (REBCNT)  in[1] <<  8    // & 0xFF00;
        | (REBCNT)  in[2] << 16    // & 0xFF0000;
        | (REBCNT)  in[3] << 24;   // & 0xFF000000;
}


//
//  Find_Int: C
//
REBCNT Find_Int(REBINT *array, REBINT num)
{
    REBCNT n;

    for (n = 0; array[n] && array[n] != num; n++);
    if (array[n]) return n;
    return NOT_FOUND;
}


//
//  Get_Num_Arg: C
// 
// Get the amount to skip or pick.
// Allow multiple types. Throw error if not valid.
// Note that the result is one-based.
//
REBINT Get_Num_Arg(REBVAL *val)
{
    REBINT n;

    if (IS_INTEGER(val)) {
        if (VAL_INT64(val) > (i64)MAX_I32 || VAL_INT64(val) < (i64)MIN_I32)
            fail (Error_Out_Of_Range(val));
        n = VAL_INT32(val);
    }
    else if (IS_DECIMAL(val) || IS_PERCENT(val)) {
        if (VAL_DECIMAL(val) > MAX_I32 || VAL_DECIMAL(val) < MIN_I32)
            fail (Error_Out_Of_Range(val));
        n = (REBINT)VAL_DECIMAL(val);
    }
    else if (IS_LOGIC(val))
        n = (VAL_LOGIC(val) ? 1 : 2);
    else
        fail (Error_Invalid_Arg(val));

    return n;
}


//
//  Float_Int16: C
//
REBINT Float_Int16(REBD32 f)
{
    if (fabs(f) > (REBD32)(0x7FFF)) {
        DS_PUSH_DECIMAL(f);
        fail (Error_Out_Of_Range(DS_TOP));
    }
    return (REBINT)f;
}


//
//  Int32: C
//
REBINT Int32(const REBVAL *val)
{
    REBINT n = 0;

    if (IS_DECIMAL(val)) {
        if (VAL_DECIMAL(val) > MAX_I32 || VAL_DECIMAL(val) < MIN_I32)
            fail (Error_Out_Of_Range(val));
        n = (REBINT)VAL_DECIMAL(val);
    } else {
        if (VAL_INT64(val) > (i64)MAX_I32 || VAL_INT64(val) < (i64)MIN_I32)
            fail (Error_Out_Of_Range(val));
        n = VAL_INT32(val);
    }

    return n;
}


//
//  Int32s: C
// 
// Get integer as positive, negative 32 bit value.
// Sign field can be
//     0: >= 0
//     1: >  0
//    -1: <  0
//
REBINT Int32s(const REBVAL *val, REBINT sign)
{
    REBINT n = 0;

    if (IS_DECIMAL(val)) {
        if (VAL_DECIMAL(val) > MAX_I32 || VAL_DECIMAL(val) < MIN_I32)
            fail (Error_Out_Of_Range(val));

        n = (REBINT)VAL_DECIMAL(val);
    } else {
        if (VAL_INT64(val) > (i64)MAX_I32 || VAL_INT64(val) < (i64)MIN_I32)
            fail (Error_Out_Of_Range(val));

        n = VAL_INT32(val);
    }

    // More efficient to use positive sense:
    if (
        (sign == 0 && n >= 0) ||
        (sign >  0 && n >  0) ||
        (sign <  0 && n <  0)
    )
        return n;

    fail (Error_Out_Of_Range(val));
}


//
//  Int64: C
//
REBI64 Int64(const REBVAL *val)
{
    if (IS_INTEGER(val))
        return VAL_INT64(val);
    if (IS_DECIMAL(val) || IS_PERCENT(val))
        return cast(REBI64, VAL_DECIMAL(val));
    if (IS_MONEY(val))
        return deci_to_int(VAL_MONEY_AMOUNT(val));

    fail (Error_Invalid_Arg(val));
}


//
//  Dec64: C
//
REBDEC Dec64(const REBVAL *val)
{
    if (IS_DECIMAL(val) || IS_PERCENT(val))
        return VAL_DECIMAL(val);
    if (IS_INTEGER(val))
        return cast(REBDEC, VAL_INT64(val));
    if (IS_MONEY(val))
        return deci_to_decimal(VAL_MONEY_AMOUNT(val));

    fail (Error_Invalid_Arg(val));
}


//
//  Int64s: C
// 
// Get integer as positive, negative 64 bit value.
// Sign field can be
//     0: >= 0
//     1: >  0
//    -1: <  0
//
REBI64 Int64s(const REBVAL *val, REBINT sign)
{
    REBI64 n;

    if (IS_DECIMAL(val)) {
        if (VAL_DECIMAL(val) > MAX_I64 || VAL_DECIMAL(val) < MIN_I64)
            fail (Error_Out_Of_Range(val));
        n = (REBI64)VAL_DECIMAL(val);
    } else {
        n = VAL_INT64(val);
    }

    // More efficient to use positive sense:
    if (
        (sign == 0 && n >= 0) ||
        (sign >  0 && n >  0) ||
        (sign <  0 && n <  0)
    )
        return n;

    fail (Error_Out_Of_Range(val));
}


//
//  Int8u: C
//
REBINT Int8u(const REBVAL *val)
{
    if (VAL_INT64(val) > cast(i64, 255) || VAL_INT64(val) < cast(i64, 0))
        fail (Error_Out_Of_Range(val));

    return VAL_INT32(val);
}


//
//  Find_Refines: C
// 
// Scans the stack for function refinements that have been
// specified in the mask (each as a bit) and are being used.
//
REBCNT Find_Refines(struct Reb_Call *call_, REBCNT mask)
{
    REBINT n;
    REBCNT result = 0;

    REBINT max = D_ARGC;

    for (n = 0; n < max; n++) {
        if ((mask & (1 << n) && D_REF(n + 1)))
            result |= 1 << n;
    }
    return result;
}


//
//  Val_Init_Datatype: C
//
void Val_Init_Datatype(REBVAL *value, enum Reb_Kind kind)
{
    *value = *CTX_VAR(Lib_Context, SYM_FROM_KIND(kind));
}


//
//  Get_Type: C
// 
// Returns the specified datatype value from the system context.
// The datatypes are all at the head of the context.
//
REBVAL *Get_Type(enum Reb_Kind kind)
{
    return CTX_VAR(Lib_Context, SYM_FROM_KIND(kind));
}


//
//  Type_Of: C
// 
// Returns the datatype value for the given value.
// The datatypes are all at the head of the context.
//
REBVAL *Type_Of(const REBVAL *value)
{
    return CTX_VAR(Lib_Context, SYM_FROM_KIND(VAL_TYPE(value)));
}


//
//  Get_Field_Name: C
// 
// Get the name of a field of an object.
//
const REBYTE *Get_Field_Name(REBCTX *context, REBCNT index)
{
    assert(index <= CTX_LEN(context));
    return Get_Sym_Name(CTX_KEY_SYM(context, index));
}


//
//  Get_Field: C
// 
// Get an instance variable from an object series.
//
REBVAL *Get_Field(REBCTX *context, REBCNT index)
{
    assert(index <= CTX_LEN(context));
    return CTX_VAR(context, index);
}


//
//  Get_Object: C
// 
// Get an instance variable from an ANY-CONTEXT! value.
//
REBVAL *Get_Object(const REBVAL *any_context, REBCNT index)
{
    REBCTX *context = VAL_CONTEXT(any_context);

    assert(GET_ARR_FLAG(CTX_VARLIST(context), SERIES_FLAG_CONTEXT));
    assert(index <= CTX_LEN(context));
    return CTX_VAR(context, index);
}


//
//  In_Object: C
// 
// Get value from nested list of objects. List is null terminated.
// Returns object value, else returns 0 if not found.
//
REBVAL *In_Object(REBCTX *base, ...)
{
    REBVAL *context = NULL;
    REBCNT n;
    va_list varargs;

    va_start(varargs, base);
    while ((n = va_arg(varargs, REBCNT))) {
        if (n > CTX_LEN(base)) {
            va_end(varargs);
            return NULL;
        }
        context = CTX_VAR(base, n);
        if (!ANY_CONTEXT(context)) {
            va_end(varargs);
            return NULL;
        }
        base = VAL_CONTEXT(context);
    }
    va_end(varargs);

    return context;
}


//
//  Get_System: C
// 
// Return a second level object field of the system object.
//
REBVAL *Get_System(REBCNT i1, REBCNT i2)
{
    REBVAL *obj;

    obj = CTX_VAR(VAL_CONTEXT(ROOT_SYSTEM), i1);
    if (i2 == 0) return obj;
    assert(IS_OBJECT(obj));
    return Get_Field(VAL_CONTEXT(obj), i2);
}


//
//  Get_System_Int: C
// 
// Get an integer from system object.
//
REBINT Get_System_Int(REBCNT i1, REBCNT i2, REBINT default_int)
{
    REBVAL *val = Get_System(i1, i2);
    if (IS_INTEGER(val)) return VAL_INT32(val);
    return default_int;
}


//
//  Make_Std_Object_Managed: C
//
REBCTX *Make_Std_Object_Managed(REBCNT index)
{
    REBCTX *context = Copy_Context_Shallow(
        VAL_CONTEXT(Get_System(SYS_STANDARD, index))
    );
    MANAGE_ARRAY(CTX_VARLIST(context));

    //
    // !!! Shallow copy... values are all the same and modifications of
    // series in one will modify all...is this right (?)
    //

    return context;
}


//
//  Set_Object_Values: C
//
void Set_Object_Values(REBCTX *context, REBVAL value[])
{
    REBVAL *var;

    var = CTX_VARS_HEAD(context);
    for (; NOT_END(var); var++) {
        if (IS_END(value)) SET_NONE(var);
        else *var = *value++;
    }
}


//
//  Val_Init_Series_Index_Core: C
// 
// Common function.
//
void Val_Init_Series_Index_Core(
    REBVAL *value,
    enum Reb_Kind type,
    REBSER *series,
    REBCNT index
) {
    assert(series);
    ENSURE_SERIES_MANAGED(series);

    if (type != REB_IMAGE && type != REB_VECTOR) {
        // Code in various places seemed to have different opinions of
        // whether a BINARY needed to be zero terminated.  It doesn't
        // make a lot of sense to zero terminate a binary unless it
        // simplifies the code assumptions somehow--it's in the class
        // "ANY_BINSTR()" so that suggests perhaps it has a bit more
        // obligation to conform.  Also, the original Make_Binary comment
        // from the open source release read:
        //
        //     Make a binary string series. For byte, C, and UTF8 strings.
        //     Add 1 extra for terminator.
        //
        // Until that is consciously overturned, check the REB_BINARY too

        ASSERT_SERIES_TERM(series); // doesn't apply to image/vector
    }

    VAL_RESET_HEADER(value, type);
    VAL_SERIES(value) = series;
    VAL_INDEX(value) = index;
}


//
//  Set_Tuple: C
//
void Set_Tuple(REBVAL *value, REBYTE *bytes, REBCNT len)
{
    REBYTE *bp;

    VAL_RESET_HEADER(value, REB_TUPLE);
    VAL_TUPLE_LEN(value) = (REBYTE)len;
    for (bp = VAL_TUPLE(value); len > 0; len--)
        *bp++ = *bytes++;
}


//
//  Val_Init_Context: C
//
// Common routine for initializing OBJECT, MODULE!, PORT!, and ERROR!
//
// A fully constructed context can reconstitute the ANY-CONTEXT! REBVAL that
// is its canon form from a single pointer...the REBVAL sitting in the 0 slot
// of the context's varlist.
//
void Val_Init_Context(REBVAL *out, enum Reb_Kind kind, REBCTX *context) {
    //
    // In a debug build we check to make sure the type of the embedded value
    // matches the type of what is intended (so someone who thinks they are
    // initializing a REB_OBJECT from a CONTEXT does not accidentally get a
    // REB_ERROR, for instance.)  It's a point for several other integrity
    // checks as well.
    //
#if !defined(NDEBUG)
    REBVAL *value = CTX_VALUE(context);

    assert(ANY_CONTEXT(value));
    assert(CTX_TYPE(context) == kind);

    assert(VAL_CONTEXT(value) == context);

    if (!CTX_KEYLIST(context)) {
        Debug_Fmt("Context found with no keylist set");
        Panic_Context(context);
    }

    assert(GET_ARR_FLAG(CTX_VARLIST(context), SERIES_FLAG_CONTEXT));

    // !!! Historically spec is a frame of an object for a "module spec",
    // may want to use another word of that and make a block "spec"
    //
    if (IS_FRAME(CTX_VALUE(context)))
        assert(ANY_FUNC(FUNC_VALUE(CTX_FUNC(context))));
    else
        assert(
            NOT(CTX_SPEC(context))
            || ANY_CONTEXT(CTX_VALUE(CTX_SPEC(context)))
        );
#endif

    // Some contexts (stack frames in particular) start out unmanaged, and
    // then check to see if an operation like Val_Init_Context set them to
    // managed.  If not, they will free the context.  This avoids the need
    // for the garbage collector to have to deal with the series if there's
    // no reason too.
    //
    // Here is a case of where we mark the context as having an extant usage,
    // so that at minimum this value must become unreachable from the root GC
    // set before they are GC'd.  For another case, see INIT_WORD_SPECIFIC(),
    // where an ANY-WORD! can mark a context as in use.
    //
    ENSURE_ARRAY_MANAGED(CTX_VARLIST(context));

    // Keylists are different, because they may-or-may-not-be-reused by some
    // operations.  There needs to be a uniform policy on their management,
    // or certain routines would return "sometimes managed, sometimes not"
    // keylist series...a bad invariant.
    //
    ASSERT_ARRAY_MANAGED(CTX_KEYLIST(context));

    *out = *CTX_VALUE(context);
}


//
//  Val_Series_Len_At: C
// 
// Get length of an ANY-SERIES! value, taking the current index into account.
// Avoid negative values.
//
REBCNT Val_Series_Len_At(const REBVAL *value)
{
    if (VAL_INDEX(value) >= VAL_LEN_HEAD(value)) return 0;
    return VAL_LEN_HEAD(value) - VAL_INDEX(value);
}


//
//  Val_Byte_Len: C
// 
// Get length of series in bytes.
//
REBCNT Val_Byte_Len(const REBVAL *value)
{
    if (VAL_INDEX(value) >= VAL_LEN_HEAD(value)) return 0;
    return (VAL_LEN_HEAD(value) - VAL_INDEX(value)) * SER_WIDE(VAL_SERIES(value));
}


//
//  Partial1: C
// 
// Process the /part (or /skip) and other length modifying
// arguments.
//
REBINT Partial1(REBVAL *sval, REBVAL *lval)
{
    REBI64 len;
    REBINT maxlen;
    REBINT is_ser = ANY_SERIES(sval);

    // If lval is not set, use the current len of the target value:
    if (IS_UNSET(lval)) {
        if (!is_ser) return 1;
        if (VAL_INDEX(sval) >= VAL_LEN_HEAD(sval)) return 0;
        return (VAL_LEN_HEAD(sval) - VAL_INDEX(sval));
    }
    if (IS_INTEGER(lval) || IS_DECIMAL(lval)) len = Int32(lval);
    else {
        if (is_ser && VAL_TYPE(sval) == VAL_TYPE(lval) && VAL_SERIES(sval) == VAL_SERIES(lval))
            len = (REBINT)VAL_INDEX(lval) - (REBINT)VAL_INDEX(sval);
        else
            fail (Error(RE_INVALID_PART, lval));
    }

    if (is_ser) {
        // Restrict length to the size available:
        if (len >= 0) {
            maxlen = (REBINT)VAL_LEN_AT(sval);
            if (len > maxlen) len = maxlen;
        } else {
            len = -len;
            if (len > (REBINT)VAL_INDEX(sval)) len = (REBINT)VAL_INDEX(sval);
            VAL_INDEX(sval) -= (REBCNT)len;
        }
    }

    return (REBINT)len;
}


//
//  Partial: C
// 
// Args:
//     aval: target value
//     bval: argument to modify target (optional)
//     lval: length value (or none)
// 
// Determine the length of a /PART value. It can be:
//     1. integer or decimal
//     2. relative to A value (bval is null)
//     3. relative to B value
// 
// NOTE: Can modify the value's index!
// The result can be negative. ???
//
REBINT Partial(REBVAL *aval, REBVAL *bval, REBVAL *lval)
{
    REBVAL *val;
    REBINT len;
    REBINT maxlen;

    // If lval is unset, use the current len of the target value:
    if (IS_UNSET(lval)) {
        val = (bval && ANY_SERIES(bval)) ? bval : aval;
        if (VAL_INDEX(val) >= VAL_LEN_HEAD(val)) return 0;
        return (VAL_LEN_HEAD(val) - VAL_INDEX(val));
    }

    if (IS_INTEGER(lval) || IS_DECIMAL(lval)) {
        len = Int32(lval);
        val = bval;
    }
    else {
        // So, lval must be relative to aval or bval series:
        if (
            VAL_TYPE(aval) == VAL_TYPE(lval)
            && VAL_SERIES(aval) == VAL_SERIES(lval)
        ) {
            val = aval;
        }
        else if (
            bval
            && VAL_TYPE(bval) == VAL_TYPE(lval)
            && VAL_SERIES(bval) == VAL_SERIES(lval)
        ) {
            val = bval;
        }
        else
            fail (Error(RE_INVALID_PART, lval));

        len = cast(REBINT, VAL_INDEX(lval)) - cast(REBINT, VAL_INDEX(val));
    }

    if (!val) val = aval;

    // Restrict length to the size available
    //
    if (len >= 0) {
        maxlen = (REBINT)VAL_LEN_AT(val);
        if (len > maxlen) len = maxlen;
    }
    else {
        len = -len;
        if (len > cast(REBINT, VAL_INDEX(val)))
            len = cast(REBINT, VAL_INDEX(val));
        VAL_INDEX(val) -= (REBCNT)len;
    }

    return len;
}


//
//  Clip_Int: C
//
int Clip_Int(int val, int mini, int maxi)
{
    if (val < mini) val = mini;
    else if (val > maxi) val = maxi;
    return val;
}

//
//  memswapl: C
// 
// For long integer memory units, not chars. It is assumed that
// the len is an exact modulo of long.
//
void memswapl(void *m1, void *m2, size_t len)
{
    long t, *a, *b;

    a = cast(long*, m1);
    b = cast(long*, m2);
    len /= sizeof(long);
    while (len--) {
        t = *b;
        *b++ = *a;
        *a++ = t;
    }
}


//
//  Add_Max: C
//
i64 Add_Max(enum Reb_Kind type, i64 n, i64 m, i64 maxi)
{
    i64 r = n + m;
    if (r < -maxi || r > maxi) {
        if (type != REB_TRASH) fail (Error(RE_TYPE_LIMIT, Get_Type(type)));
        r = r > 0 ? maxi : -maxi;
    }
    return r;
}


//
//  Mul_Max: C
//
int Mul_Max(enum Reb_Kind type, i64 n, i64 m, i64 maxi)
{
    i64 r = n * m;
    if (r < -maxi || r > maxi) fail (Error(RE_TYPE_LIMIT, Get_Type(type)));
    return (int)r;
}


//
//  Make_OS_Error: C
//
void Make_OS_Error(REBVAL *out, int errnum)
{
    REBCHR str[100];

    OS_FORM_ERROR(errnum, str, 100);
    Val_Init_String(out, Copy_OS_Str(str, OS_STRLEN(str)));
}


//
//  Collect_Set_Words: C
// 
// Scan a block, collecting all of its SET words as a block.
//
REBARR *Collect_Set_Words(REBVAL *val)
{
    REBCNT count = 0;
    REBVAL *val2 = val;
    REBARR *array;

    for (; NOT_END(val); val++) if (IS_SET_WORD(val)) count++;
    val = val2;

    array = Make_Array(count);
    val2 = ARR_HEAD(array);
    for (; NOT_END(val); val++) {
        if (IS_SET_WORD(val))
            Val_Init_Word(val2++, REB_WORD, VAL_WORD_SYM(val));
    }
    SET_END(val2);
    SET_ARRAY_LEN(array, count);

    return array;
}


//
//  What_Reflector: C
//
REBINT What_Reflector(REBVAL *word)
{
    if (IS_WORD(word)) {
        switch (VAL_WORD_SYM(word)) {
        case SYM_SPEC:   return OF_SPEC;
        case SYM_BODY:   return OF_BODY;
        case SYM_WORDS:  return OF_WORDS;
        case SYM_VALUES: return OF_VALUES;
        case SYM_TYPES:  return OF_TYPES;
        case SYM_TITLE:  return OF_TITLE;
        }
    }
    return 0;
}
