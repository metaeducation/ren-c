REBOL [
    File: %cflags-map.r
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Abstract C Compiler Flag Mappings"
    Rights: --{
        Copyright 2012-2025 Rebol Open Source Contributos
        REBOL is a trademark of REBOL Technologies
    }--
    License: --{
        Licensed under the Lesser GPL, Version 3.0
        See: https://www.gnu.org/licenses/lgpl-3.0.html
    }--
    Purpose: --{
        This file defines TAG!s that you can use in the cflags specifications
        for the build, which abstract different literal flag text based on
        which compiler you are using.
    }--
]

<no-uninitialized> => [
    <gnu:-Wno-uninitialized>

    ;-Wno-unknown-warning seems to only modify the
    ; immediately following option
    ;
    ;<gnu:-Wno-unknown-warning>
    ;<gnu:-Wno-maybe-uninitialized>

    <msc:/wd4701> <msc:/wd4703>
]

<no-sign-compare> => [
    <gnu:-Wno-sign-compare>
    <msc:/wd4388>
    <msc:/wd4018>  ; a 32-bit variant of the error
]

<implicit-fallthru> => [
    <gnu:-Wno-unknown-warning>
    <gnu:-Wno-implicit-fallthrough>
]

<no-unused-parameter> => <gnu:-Wno-unused-parameter>

<no-shift-negative-value> => <gnu:-Wno-shift-negative-value>

<no-unreachable> => <msc:/wd4702>

<no-hidden-local> => <msc:/wd4456>

<no-constant-conditional> => <msc:/wd4127>

; !!! This is a special signal to tell the make process not to scan a
; source file for function prototypes, natives, or generics.  It probably
; shouldn't be done with a TAG! like this, but it was expedient at some
; point in history to do it this way.
;
<no-make-header> => []
