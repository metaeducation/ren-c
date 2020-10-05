REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Create C .inc file with const data of r3.exe startup code"
    File: %prep-main.reb
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        This compresses together several pieces of Rebol code (with comments
        stripped out), and then turns it into a static C constant byte array.
        It is decompressed by the code in %main.c, which then executes it.

        The process uses much of the same code that embeds the mezzanine into
        the core library.  Notably, it makes very light use of the bootstrap
        Rebol's LOAD function.  Hence code can use arbitrarily modern syntax
        even with an older Rebol being used for the build.
    }
]

do %../../tools/common.r  ; for PARSE-ARGS, STRIPLOAD, BINARY-TO-C...
do %../../tools/common-emitter.r  ; for splicing Rebol into templated strings

args: parse-args system/script/args  ; either from command line or DO/ARGS
output-dir: make-file [(system/options/path) prep /]
mkdir/deep make-file [(output-dir) main /]


; !!! The "host protocols" are embedded in a way that is very similar to the
; extensions.  However, they do not have any native code directly associated
; with them.  It would be nice to unify this, to blur the distinction between
; an "extension" and a "module" a bit more.
;
; But what's happening here is just that the header and contents of the
; modules are being put into blocks so that they can be instantiated at the
; right moment, by reading blocks out of the HOST-PROT variable.

buf: make text! 200000

comment [  ; if LOAD were used this would need a header
    append/line buf "REBOL []"
]

append/line buf "host-prot: ["


for-each file [
    %../../scripts/prot-tls.r  ; TLS (a.k.a. the "S" in HTTPS)
    %../../scripts/prot-http.r  ; HTTP Client (HTTPS if used with TLS)
][
    header: _  ; was a SET-WORD!...for locals gathering?
    contents: stripload/header (join %../mezz/ file) 'header

    ; We go ahead and LOAD the header in this case, so we can write only the
    ; module fields we care about ("Description" is not needed, for instance.)
    ;
    header: load header
    append/line buf mold/flat compose [
        Title: (header/title)
        Version: (header/version)
        Name: (header/name)
    ]

    append/line buf "["
    append/line buf contents
    append/line buf "]"
]

append/line buf "]"


; In order to get the whole process rolling for the r3.exe, it needs to do
; command line processing and read any encapped code out of the executable.
; It would likely want to build on ZIP files to do this, so the unzip script
; is embedded as well.
;
; !!! Unlike the %prot-http.r and %prot-tls.r, this would be a difficult
; process to abstract to extensions...since de-encapping might be important
; to booting to the point of being able to load anything at all.

for-each file [
    %../../scripts/encap.reb
    %../../scripts/unzip.reb
    %main-startup.reb
][
    print ["loading:" file]

    header: _  ; !!! Was a SET-WORD!...for locals gathering?
    contents: stripload/header file 'header

    is-module: false  ; currently none of these three files are modules
    if is-module [
        append/line buf "import module ["
        append/line buf header
        append/line buf "]["
        append/line buf contents
        append/line buf "]"
    ] else [
        append/line buf contents
    ]
]

; The code evaluates to the MAIN-STARTUP function, so it is easily found
; as the result of running the code in %main.c - make it the last line.
;
; (This organization lets us separate the moment of loading from the moment
; of running, in case that were interesting.)
;
append/line buf ":main-startup"


; It's helpful to have an uncompressed readable copy of the bundled and
; stripped init code for inspection.
;
write-if-changed make-file [(output-dir) main /tmp-main-startup.r] buf


(e: make-emitter
    "r3 console executable embedded Rebol code bundle"
    output-dir/main/tmp-main-startup.inc)

compressed: gzip buf

e/emit 'compressed {
    /*
     * Gzip compression of host initialization code
     * Originally $<length of buf> bytes
     */
    #define MAIN_STARTUP_SIZE $<length of compressed>
    const unsigned char Main_Startup_Code[MAIN_STARTUP_SIZE] = {
        $<Binary-To-C Compressed>
    };
}

e/write-emitted
