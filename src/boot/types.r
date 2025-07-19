Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Rebol datatypes and their related attributes"
    rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2018 Rebol Open Source Developers
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    purpose: {
        This table is used to make C defines and intialization tables.

        name        - name of datatype (generates words)
        class       - how type actions are dispatched (T_type), * is extension
        path        - it supports various path forms (+ for same as typeclass)
        make        - It can be made with #[datatype] method
        typesets    - what typesets the type belongs to

        Note that if there is `somename` in the class column, that means you
        will find the ACTION! dispatch for that type in `REBTYPE(Somename)`.
    }
    macros: {
        /*
        ** ORDER-DEPENDENT TYPE MACROS, e.g. ANY_BLOCK_KIND() or IS_BINDABLE()
        **
        ** These macros embed specific knowledge of the type ordering.  They
        ** are specified in %types.r, so anyone changing the order of types is
        ** more likely to notice the impact, and adjust them.
        **
        ** !!! Review how these might be auto-generated from the table.
        */

        /* We use Unchecked_Type_Of() for checking the bindable flag because it
           is called *extremely often*; the extra debug checks in Type_Of()
           make it prohibitively more expensive than a simple check of a
           flag, while these tests are very fast. */

        #define Is_Bindable(v) \
            (Unchecked_Type_Of(v) < TYPE_INTEGER)

        #define Not_Bindable(v) \
            (Unchecked_Type_Of(v) >= TYPE_INTEGER)

        /* For other checks, we pay the cost in the debug build of all the
           associated baggage that Type_Of() carries over Unchecked_Type_Of() */

        INLINE bool Any_Element_Type(Type t) {
            assert(t < TYPE_MAX and t != TYPE_0);
            return t < TYPE_TRASH;
        }

        #define Any_Element(v) \
            Any_Element_Type(Type_Of(v))

        #define Any_Antiform_Type(k) \
            (not Any_Element_Type(k))

        #define Is_Antiform(v) \
            (not Any_Element(v))

        INLINE bool Any_Scalar_Type(Type t) {
            return t >= TYPE_INTEGER and t <= TYPE_DATE;
        }

        #define Any_Scalar(v) \
            Any_Scalar_Type(Type_Of(v))

        INLINE bool Any_Series_Type(Type t) {
            return t >= TYPE_PATH and t <= TYPE_BITSET;
        }

        #define Any_Series(v) \
            Any_Series_Type(Type_Of(v))

        INLINE bool Any_String_Type(Type t) {
            return t >= TYPE_TEXT and t <= TYPE_TAG;
        }

        #define Any_String(v) \
            Any_String_Type(Type_Of(v))

        INLINE bool Any_List_Type(Type t) {
            return t >= TYPE_PATH and t <= TYPE_BLOCK;
        }

        #define Any_List(v) \
            Any_List_Type(Type_Of(v))

        INLINE bool Any_Word_Type(Type t) {
            return t >= TYPE_WORD and t <= TYPE_ISSUE;
        }

        #define Any_Word(v) \
            Any_Word_Type(Type_Of(v))

        INLINE bool Any_Path_Type(Type t) {
            return t >= TYPE_PATH and t <= TYPE_LIT_PATH;
        }

        #define Any_Path(v) \
            Any_Path_Type(Type_Of(v))

        INLINE bool Any_Context_Type(Type t) {
            return t >= TYPE_OBJECT and t <= TYPE_PORT;
        }

        #define Any_Context(v) \
            Any_Context_Type(Type_Of(v))

        /* !!! There was an IS_NUMBER() macro defined in R3-Alpha which was
           TYPE_INTEGER and TYPE_DECIMAL.  But ANY-NUMBER! the typeset included
           PERCENT! so this adds that and gets rid of IS_NUMBER() */

        INLINE bool Any_Number_Type(Type t) {
            return t == TYPE_INTEGER or t == TYPE_DECIMAL or t == TYPE_PERCENT;
        }

        #define Any_Number(v) \
            Any_Number_Type(Type_Of(v))

        /* !!! Being able to locate inert types based on range *almost* works,
           but TYPE_ISSUE and TYPE_REFINEMENT want to be picked up as ANY-WORD!.
           This trick will have to be rethought, esp if words and strings
           get unified, but it's here to show how choosing these values
           carefully can help with speeding up tests. */

        INLINE bool Any_Inert_Type(Type t) {
            return (t >= TYPE_BLOCK and t <= TYPE_BLANK)
                or t == TYPE_ISSUE or t == TYPE_REFINEMENT;
        }

        #define Any_Inert(v) \
            Any_Inert_Type(Type_Of(v))
    }
]


[name       class       path    make    mold     typesets]

; Note: 0 is reserved for an array terminator (TYPE_0), and not a "type"

; There is only one "invokable" type in Ren-C, and it takes the name ACTION!
; instead of the name FUNCTION!: https://forum.rebol.info/t/596

action      action      +       +       +       -

; ANY-WORD!, order matters (tests like ANY_WORD use >= TYPE_WORD, <= TYPE_ISSUE)
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
blob        string      +       +       blob    [series]
text        string      +       +       +       [series string]
file        string      +       +       +       [series string]
email       string      +       +       +       [series string]
url         string      +       +       +       [series string]
money       string      +       +       +       [series string]
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

integer     integer     -       +       +       [number scalar]
decimal     decimal     -       +       +       [number scalar]
percent     decimal     -       +       +       [number scalar]
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

; === BEGIN "ANTIFORMS" ===
;
; The bootstrap executable has no concept of antiforms, so they're just
; another kind of cell that is periodically checked to make sure it does not
; occur in source locations.
;
trash       unit        -       +       +       -
void        unit        -       +       +       -
okay        unit        -       +       +       -
nulled      unit        -       +       +       -

; Note that the "null?" state has no associated NULL! datatype.  Internally
; it uses TYPE_MAX, but like the TYPE_0 it stays off the type map.
