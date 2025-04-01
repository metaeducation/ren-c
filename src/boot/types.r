REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Rebol datatypes and their related attributes"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2018 Rebol Open Source Developers
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        This table is used to make C defines and intialization tables.

        name        - name of datatype (generates words)
        class       - how type actions are dispatched (T_type), * is extension
        path        - it supports various path forms (+ for same as typeclass)
        make        - It can be made with #[datatype] method
        typesets    - what typesets the type belongs to

        Note that if there is `somename` in the class column, that means you
        will find the ACTION! dispatch for that type in `REBTYPE(Somename)`.
    }
    Macros: {
        /*
        ** ORDER-DEPENDENT TYPE MACROS, e.g. ANY_BLOCK_KIND() or IS_BINDABLE()
        **
        ** These macros embed specific knowledge of the type ordering.  They
        ** are specified in %types.r, so anyone changing the order of types is
        ** more likely to notice the impact, and adjust them.
        **
        ** !!! Review how these might be auto-generated from the table.
        */

        /* We use VAL_TYPE_RAW() for checking the bindable flag because it
           is called *extremely often*; the extra debug checks in VAL_TYPE()
           make it prohibitively more expensive than a simple check of a
           flag, while these tests are very fast. */

        #define Is_Bindable(v) \
            (VAL_TYPE_RAW(v) < REB_LOGIC)

        #define Not_Bindable(v) \
            (VAL_TYPE_RAW(v) >= REB_LOGIC)

        /* For other checks, we pay the cost in the debug build of all the
           associated baggage that VAL_TYPE() carries over VAL_TYPE_RAW() */

        #define Any_Value(v) \
            (VAL_TYPE(v) != REB_MAX_NULLED)

        INLINE bool Any_Scalar_Kind(enum Reb_Kind k) {
            return k >= REB_LOGIC and k <= REB_DATE;
        }

        #define Any_Scalar(v) \
            Any_Scalar_Kind(VAL_TYPE(v))

        INLINE bool Any_Series_Kind(enum Reb_Kind k) {
            return k >= REB_PATH and k <= REB_BITSET;
        }

        #define Any_Series(v) \
            Any_Series_Kind(VAL_TYPE(v))

        INLINE bool Any_String_Kind(enum Reb_Kind k) {
            return k >= REB_TEXT and k <= REB_TAG;
        }

        #define Any_String(v) \
            Any_String_Kind(VAL_TYPE(v))

        INLINE bool Any_List_Kind(enum Reb_Kind k) {
            return k >= REB_PATH and k <= REB_BLOCK;
        }

        #define Any_List(v) \
            Any_List_Kind(VAL_TYPE(v))

        INLINE bool Any_Word_Kind(enum Reb_Kind k) {
            return k >= REB_WORD and k <= REB_ISSUE;
        }

        #define Any_Word(v) \
            Any_Word_Kind(VAL_TYPE(v))

        INLINE bool Any_Path_Kind(enum Reb_Kind k) {
            return k >= REB_PATH and k <= REB_LIT_PATH;
        }

        #define Any_Path(v) \
            Any_Path_Kind(VAL_TYPE(v))

        INLINE bool Any_Context_Kind(enum Reb_Kind k) {
            return k >= REB_OBJECT and k <= REB_PORT;
        }

        #define Any_Context(v) \
            Any_Context_Kind(VAL_TYPE(v))

        /* !!! There was an IS_NUMBER() macro defined in R3-Alpha which was
           REB_INTEGER and REB_DECIMAL.  But ANY-NUMBER! the typeset included
           PERCENT! so this adds that and gets rid of IS_NUMBER() */

        INLINE bool Any_Number_Kind(enum Reb_Kind k) {
            return k == REB_INTEGER or k == REB_DECIMAL or k == REB_PERCENT;
        }

        #define Any_Number(v) \
            Any_Number_Kind(VAL_TYPE(v))

        /* !!! Being able to locate inert types based on range *almost* works,
           but REB_ISSUE and REB_REFINEMENT want to be picked up as ANY-WORD!.
           This trick will have to be rethought, esp if words and strings
           get unified, but it's here to show how choosing these values
           carefully can help with speeding up tests. */

        INLINE bool Any_Inert_Kind(enum Reb_Kind k) {
            return (k >= REB_BLOCK and k <= REB_BLANK)
                or k == REB_ISSUE or k == REB_REFINEMENT;
        }

        #define Any_Inert(v) \
            Any_Inert_Kind(VAL_TYPE(v))
    }
]


[name       class       path    make    mold     typesets]

; Note: 0 is reserved for an array terminator (REB_0), and not a "type"

; There is only one "invokable" type in Ren-C, and it takes the name ACTION!
; instead of the name FUNCTION!: https://forum.rebol.info/t/596

action      action      +       +       +       -

; ANY-WORD!, order matters (tests like ANY_WORD use >= REB_WORD, <= REB_ISSUE)
;
word        word        +       +       +       word
set-word    word        +       +       +       word
get-word    word        +       +       +       word
lit-word    word        +       +       +       word
refinement  word        +       +       +       word
issue       word        +       +       +       word

; ANY-LIST!, order matters (and contiguous with ANY-SERIES below matters!)
;
path        list        +       +       +       [series path list]
set-path    list        +       +       +       [series path list]
get-path    list        +       +       +       [series path list]
lit-path    list        +       +       +       [series path list]
group       list        +       +       +       [series list]
; -- start of inert bindable types (that aren't refinement! and issue!)
block       list        +       +       +       [series list]

; ANY-SERIES!, order matters (and contiguous with ANY-LIST above matters!)
;
binary      string      +       +       binary  [series]
text        string      +       +       +       [series string]
file        string      +       +       +       [series string]
email       string      +       +       +       [series string]
url         string      +       +       +       [series string]
tripwire    string      +       +       +       [series string]
tag         string      +       +       +       [series string]

bitset      bitset      +       +       +       -

map         map         +       +       +       -

varargs     varargs     +       +       +       -

object      context     +       +       +       context
frame       context     +       +       +       context
module      context     +       +       +       context
error       context     +       +       error   context
port        port        context +       context context

; ^-------- Everything above is a "bindable" type, see Is_Bindable() --------^

; v------- Everything below is an "unbindable" type, see Is_Bindable() ------v

; scalars

logic       logic       -       +       +       -
integer     integer     -       +       +       [number scalar]
decimal     decimal     -       +       +       [number scalar]
percent     decimal     -       +       +       [number scalar]
money       money       -       +       +       scalar
char        char        -       +       +       scalar
pair        pair        +       +       +       scalar
tuple       tuple       +       +       +       scalar
time        time        +       +       +       scalar
date        date        +       +       +       -

; type system

datatype    datatype    -       +       +       -
typeset     typeset     -       +       +       -

; things likely to become user-defined types or extensions

event       event       +       +       +       -
handle      handle      -       -       +       -

; "unit types" https://en.wikipedia.org/wiki/Unit_type

blank       unit        blank   +       +       -
; end of inert unbindable types
nothing     unit        -       +       +       -
void        unit        -       +       +       -

; Note that the "null?" state has no associated NULL! datatype.  Internally
; it uses REB_MAX, but like the REB_0 it stays off the type map.
