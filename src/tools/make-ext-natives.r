REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate extention native header files"
    File: %make-ext-native.r ;-- EMIT-HEADER uses to indicate emitting script
    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Needs: 2.100.100
    Description: {
        This script is used to preprocess C source files containing code for
        extension DLLs, designed to load new native code into the interpreter.
        
        Such code is very similar to that of the code which is built into
        the EXE itself.  Hence, features like scanning the C comments for
        native specifications is reused.
    }
]

do %r2r3-future.r
do %common.r
do %common-emitter.r
do %form-header.r


args: parse-args system/options/args

m-name: ensure string! args/MODULE
l-m-name: lowercase copy m-name
u-m-name: uppercase copy m-name

c-src: fix-win32-path to file! ensure string! args/SRC

print ["building" m-name "from" c-src]


output-dir: fix-win32-path to file! any [:args/OUTDIR %../]
mkdir/deep output-dir/include


verbose: false


; The way that the processing code for extracting Rebol information out of
; C file comments is written is that the PROTO-PARSER has several callback
; functions that can be registered to receive each item it detects.
;

do %common-parsers.r
do %native-emitters.r ;for emit-native-proto and emit-include-params-macro

proto-count: 0
module-header: _

source.text: read c-src
if system/version > 2.100.0 [ ;-- !!! Why is this necessary?
    source.text: deline to-string source.text
] 

; When the header information in the comments at the top of the file is
; seen, save it into a variable.
;

proto-parser/emit-fileheader: func [header] [module-header: header]

; Reuse the emitter that is used on processing natives in the core source.
; It will add the information to UNSORTED-BUFFER
;
c-natives: make block! 128
unsorted-buffer: make string! 20000
proto-parser/emit-proto: :emit-native-proto
    
the-file: c-src ;-- global used for comments in the native emitter

proto-parser/process source.text


;
; At this point the natives will all be in the UNSORTED-BUFFER.  Extensions
; have added a concept of adding words (as from %words.r) for use as symbols,
; as well as errors--both possible to specify in the C comments just like
; the native headers have.
;

native-list: load unsorted-buffer
word-list: copy []
export-list: copy []
error-list: copy []
num-native: 0
unless parse native-list [
    while [
        set w set-word!
        [
            'native block!
                |
            'native/body 2 block!
                |
                [
                'native/export block!
                    |
                'native/export/body 2 block!
                    |
                'native/body/export 2 block!
            ]
            (append export-list to word! w)
        ]
        (++ num-native)
            |
        remove [
            quote new-words: set words block! (append word-list words)
        ]
            |
        remove [
            quote new-errors: set errors block! (append error-list errors)
        ]
    ]
][
    fail [
        "failed to parse" mold native-list ":"
        "current word-list:" mold word-list
    ]
]
;print ["specs:" mold native-list]
word-list: unique word-list
spec: compose/deep/only [
    REBOL [
        name: (to word! m-name)
        exports: (export-list)
    ]
]
unless empty? word-list [
    append spec compose/only [
        words: (word-list)
    ]
]
unless empty? error-list [
    append spec compose/only [
        errors: (error-list)
    ]
]
append spec native-list
comp-data: compress data: to-binary mold spec
;print ["buf:" to string! data]

emit-header m-name join-all [%tmp-mod- l-m-name %-last.h]
emit-lines [
    [{int Module_Init_} m-name {(RELVAL *out);}]
    [{int Module_Quit_} m-name {(void);}]
    ["#if !defined(MODULE_INCLUDE_DECLARATION_ONLY)"]
    ["#define EXT_NUM_NATIVES_" u-m-name space num-native]
    ["#define EXT_NAT_COMPRESSED_SIZE_" u-m-name space length-of comp-data]
    [
        "const REBYTE Ext_Native_Specs_" m-name
        "[EXT_NAT_COMPRESSED_SIZE_" u-m-name "] = {"
    ]
]

;-- Convert UTF-8 binary to C-encoded string:
emit binary-to-c comp-data
emit-line "};" ;-- EMIT-END erases the last comma, but there's no extra

either num-native > 0 [
    emit-line [
        "REBNAT Ext_Native_C_Funcs_" m-name
        "[EXT_NUM_NATIVES_" u-m-name "] = {"
    ]
    for-each item native-list [
        if set-word? item [
            emit-item ["N_" to word! item]
        ]
    ]
    emit-end
][
    emit-line ["REBNAT *Ext_Native_C_Funcs_" m-name space "= NULL;"]
]

emit-line [ {
int Module_Init_} m-name {(RELVAL *out)
^{
    INIT_} u-m-name {_WORDS;}
either empty? error-list [ unspaced [ {
    REBARR * arr = Make_Extension_Module_Array(
        Ext_Native_Specs_} m-name {, EXT_NAT_COMPRESSED_SIZE_} u-m-name {,
        Ext_Native_C_Funcs_} m-name {, EXT_NUM_NATIVES_} u-m-name {,
        0);} ]
    ][
        unspaced [ {
    Ext_} m-name {_Error_Base = Find_Next_Error_Base_Code();
    assert(Ext_} m-name {_Error_Base > 0);
    REBARR * arr = Make_Extension_Module_Array(
        Ext_Native_Specs_} m-name {, EXT_NAT_COMPRESSED_SIZE_} u-m-name {,
        Ext_Native_C_Funcs_} m-name {, EXT_NUM_NATIVES_} u-m-name {,
        Ext_} m-name {_Error_Base);}]
    ] {
    if (!IS_BLOCK(out)) {
        Init_Block(out, arr);
    } else {
        Append_Values_Len(VAL_ARRAY(out), KNOWN(ARR_HEAD(arr)), ARR_LEN(arr));
        Free_Array(arr);
    }

    return 0;
^}

int Module_Quit_} m-name {(void)
{
    return 0;
}
#endif //MODULE_INCLUDE_DECLARATION_ONLY
}
]

write-emitted to file! unspaced [
    output-dir/include/tmp-mod- l-m-name %-last.h
]

;--------------------------------------------------------------
; args

emit-header
    "PARAM() and REFINE() Automatic Macros"
    to file! unspaced [%tmp-mod- l-m-name %-first.h]

emit-native-include-params-macro native-list

;--------------------------------------------------------------
; words
emit-lines [
    ["//  Local words"]
    ["#define NUM_EXT_" u-m-name "_WORDS" space length-of word-list]
]

either empty? word-list [
    emit-line ["#define INIT_" u-m-name "_WORDS"]
][
    emit-line [
        "static const char* Ext_Words_" m-name
        "[NUM_EXT_" u-m-name "_WORDS] = {"
    ]
    for-next word-list [
        emit-line/indent [ {"} to string! word-list/1 {",} ]
    ]
    emit-end

    emit-line [
        "static REBSTR* Ext_Canons_" m-name
        "[NUM_EXT_" u-m-name "_WORDS];"
    ]

    word-seq: 0
    for-next word-list [
        emit-line [
            "#define"
            space
            u-m-name {_WORD_} uppercase to-c-name word-list/1
            space
            {Ext_Canons_} m-name {[} word-seq {]}
        ]
        ++ word-seq
    ]
    emit-line ["#define INIT_" u-m-name "_WORDS" space "\"]
    emit-line/indent [
        "Init_Extension_Words("
            "cast(const REBYTE**, Ext_Words_" m-name ")"
            "," space
            "Ext_Canons_" m-name
            "," space
            "NUM_EXT_" u-m-name "_WORDS"
        ")"
    ]
]

;--------------------------------------------------------------
; errors

emit-line ["//  Local errors"]
unless empty? error-list [
    emit-line [ {enum Ext_} m-name {_Errors ^{}]
    error-collected: copy []
    for-each [key val] error-list [
        unless set-word? key [
            fail ["key (" mold key ") must be a set-word!"]
        ]
        if find error-collected key [
            fail ["Duplicate error key" to word! key]
        ]
        append error-collected key
        emit-item/upper [
            {RE_EXT_ENUM_} u-m-name {_} to-c-name to word! key
        ]
    ]
    emit-end
    emit-line ["static REBINT Ext_" m-name "_Error_Base;"]

    emit-line []
    for-each [key val] error-list [
        key: uppercase to-c-name to word! key
        emit-line [
            {#define RE_EXT_} u-m-name {_} key
            space
            {(}
            {Ext_} m-name {_Error_Base + RE_EXT_ENUM_} u-m-name {_} key
            {)}
        ]
    ]
]

write-emitted to file! unspaced [
    output-dir/include/tmp-mod- l-m-name %-first.h
]
