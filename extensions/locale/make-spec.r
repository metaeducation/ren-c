REBOL []

name: 'Locale
source: [
    %locale/mod-locale.c

    ; The locale module uses non-constant aggregate initialization,
    ; e.g. LOCALE_WORD_ALL is defined as Ext_Canons_Locale[4], but
    ; is assigned as `= {{LOCALE_WORD_ALL, LC_ALL}...}` to a struct.
    ; For the moment, since it's just the locale module, disable the
    ; warning, though we don't want to use nonstandard C as a general
    ; rule in the core.
    ;
    ;    nonstandard extension used : non-constant aggregate
    ;    initializer
    ;
    <msc:/wd4204>

    <msc:/wd4459>  ; global shadowing ok, see LIBREBOL_SPECIFIER
]
includes: copy [
    %prep/extensions/locale ;for %tmp-extensions-locale-init.inc
]

requires: 'process ;for get-env

use-librebol: true
