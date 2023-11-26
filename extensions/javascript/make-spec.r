REBOL []

name: 'JavaScript
source: [
    %javascript/mod-javascript.c

    ; This is where you'd put warning disablements, etc.
    ;
    ; <msc:/wd4255>
]
includes: [
    %prep/extensions/javascript ;for %tmp-ext-odbc-init.inc
]
libraries: []

options: [
    ; option-name [word! logic? blank!] ()
]
