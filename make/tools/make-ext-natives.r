REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate extention native header files"
    File: %make-ext-native.r ;-- EMIT-HEADER uses to indicate emitting script
    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017-2018 Rebol Open Source Contributors
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
do %systems.r

; The way that the processing code for extracting Rebol information out of
; C file comments is written is that the PROTO-PARSER has several callback
; functions that can be registered to receive each item it detects.
;

do %common-parsers.r
do %native-emitters.r ; for emit-include-params-macro

args: parse-args system/options/args

config: config-system try get 'args/OS_ID

mod: ensure string! args/MODULE
m-name: mod
l-m-name: lowercase copy m-name
u-m-name: uppercase copy m-name

c-src: join-of %../../src/ fix-win32-path to file! ensure string! args/SRC

print ["building" m-name "from" c-src]

output-dir: system/options/path/prep
mkdir/deep output-dir/include

e1: (make-emitter "Module C Header File Preface"
    ensure file! join-all [output-dir/include/tmp-mod- l-m-name %-first.h])

e2: (make-emitter "Module C Header File Epilogue"
    ensure file! join-all [output-dir/include/tmp-mod- l-m-name %-last.h])


verbose: false

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
; At this point the natives will all be in the UNSORTED-BUFFER.
;

native-list: load unsorted-buffer
;print ["*** specs:" mold native-list]

native-spec: make object! [
    spec: _
    errors: _
    platforms: _
    name: _
]

native-specs: copy []

spec: _
errors: _
platforms: _
n-name: _
n-spec: _
native-list-rule: [
    while [
        set w set-word! copy spec [
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
        ](
            if not blank? n-name [
                ;dump n-name
                append native-specs make native-spec compose/only [
                    name: (to lit-word! n-name)
                    spec: (copy n-spec)
                    errors: (either errors [copy errors][_])
                    platforms: (either platforms [copy platforms][_])
                ]
            ]

            n-name: w
            n-spec: spec
            spec: _
            errors: _
            platforms: _
        )
            |
        remove [
            quote new-errors: set errors block!
        ]
            |
        remove [
            quote platforms: set platforms block!
        ]
    ]
]

if not parse native-list native-list-rule [
    fail [
        "failed to parse" mold native-list
    ]
]

if not blank? n-name [
    ;dump n-name
    append native-specs make native-spec compose/only [
        name: (to lit-word! n-name)
        spec: (copy n-spec)
        errors: (either errors [copy errors][_])
        platforms: (either platforms [copy platforms][_])
    ]
]

clear native-list
export-list: copy []
error-list: copy []
num-native: 0
for-each native native-specs [
    ;dump native
    if not blank? native/platforms [
        supported?: false
        for-each plat native/platforms [
            case [
                word? plat [; could be os-base or os-name
                    if find reduce [config/os-name config/os-base] plat [
                        supported?: true
                        break
                    ]
                ]
                path? plat [; os-base/os-name format
                    if plat = to path! reduce [config/os-base config/os-name][
                        supported?: true
                        break
                    ]
                ]
                true [
                    fail ["Unrecognized platform spec:" mold plat]
                ]
            ]
        ]
        if not supported? [continue]
    ]

    num-native: num-native + 1
    ;dump (to block! first native/spec)
    if find to block! first native/spec 'export [
        append export-list to word! native/name
    ]
    if not blank? native/errors [append error-list native/errors]
    append native-list reduce [to set-word! native/name]
    append native-list native/spec
]

;print ["specs:" mold native-list]
spec: compose/deep/only [
    REBOL [
        name: (to word! m-name)
        exports: (export-list)
    ]
]
if not empty? error-list [
    append spec compose/only [
        errors: (error-list)
    ]
]
append spec native-list
comp-data: gzip data: to-binary mold spec
;print ["buf:" to string! data]

e2/emit {
    int Module_Init_${Mod}(RELVAL *out);
    int Module_Quit_${Mod}(void);
    
    #if !defined(MODULE_INCLUDE_DECLARATION_ONLY)
    
    #define EXT_NUM_NATIVES_${MOD} $(num-native)
    #define EXT_NAT_COMPRESSED_SIZE_${MOD} $(length-of comp-data)
    
    const REBYTE Ext_Native_Specs_${Mod}[EXT_NAT_COMPRESSED_SIZE_${MOD}] = ^{
}

;-- Convert UTF-8 binary to C-encoded string:
e2/emit binary-to-c comp-data
e2/emit-line "};" ;-- EMIT-END erases last comma, but there's no extra

either num-native > 0 [
    e2/emit ["REBNAT Ext_Native_C_Funcs_${Mod}[EXT_NUM_NATIVES_${MOD}] = {"]
    for-each item native-list [
        if set-word? item [
            e2/emit-item ["N_" u-m-name "_" to word! item]
        ]
    ]
    e2/emit-end
][
    e2/emit ["REBNAT *Ext_Native_C_Funcs_${Mod} = NULL;"]
]

e2/emit {
    int Module_Init_${Mod}(RELVAL *out) {
        Ext_${Mod}_Error_Base = Find_Next_Error_Base_Code();
        assert(Ext_${Mod}_Error_Base > 0);
        REBARR *arr = Make_Extension_Module_Array(
            Ext_Native_Specs_${Mod}, EXT_NAT_COMPRESSED_SIZE_${MOD},
            Ext_Native_C_Funcs_${Mod}, EXT_NUM_NATIVES_${MOD},
            Ext_${Mod}_Error_Base
        );
        if (!IS_BLOCK(out))
            Init_Block(out, arr);
        else {
            Append_Values_Len(
                VAL_ARRAY(out),
                KNOWN(ARR_HEAD(arr)),
                ARR_LEN(arr)
            );
            Free_Array(arr);
        }
        return 0;
    }

    int Module_Quit_${Mod}(void) {
        return 0;
    }

    #endif // MODULE_INCLUDE_DECLARATION_ONLY
}

e2/write-emitted


e1/emit {
    /*
    ** INCLUDE_PARAMS_OF MACROS: DEFINING PARAM(), REF(), ARG()
    */
}
e1/emit-line []

for-next native-list [
    if tail? next native-list [break]

    if any [
        'native = native-list/2
        all [path? native-list/2 | 'native = first native-list/2]
    ][
        assert [set-word? native-list/1]
        (emit-include-params-macro/ext e1
            (to-word native-list/1) (native-list/3)
            u-m-name)
        e1/emit newline
    ]
]


e1/emit {
    /*
    ** REDEFINE REBNATIVE MACRO LOCALLY TO INCLUDE EXTENSION NAME
    **
    ** This avoids name collisions with the core, or with other extensions.
    **/

    #undef REBNATIVE
    #define REBNATIVE(n) \
        REB_R N_${MOD}_##n(REBFRM *frame_)
}
e1/emit-line []


e1/emit {
    /*
    ** EXTENSION-DEFINED ERRORS
    */

    static REBINT Ext_${Mod}_Error_Base;
}

if not empty? error-list [
    e1/emit ["enum Ext_${Mod}_Errors {"]
    error-collected: copy []
    for-each [key val] error-list [
        if not set-word? key [
            fail ["key (" mold key ") must be a set-word!"]
        ]
        if find error-collected key [
            fail ["Duplicate error key" (to word! key)]
        ]
        append error-collected key
        e1/emit-item/upper [
            {RE_EXT_ENUM_} u-m-name {_} to-c-name to word! key
        ]
    ]
    e1/emit-end
]

e1/emit-line []
for-each [key val] error-list [
    key: to-word key
    e1/emit 'key {
        #define RE_EXT_${MOD}_${KEY} \
            (Ext_${Mod}_Error_Base + RE_EXT_ENUM_${MOD}_${KEY})
    }
]

e1/write-emitted
