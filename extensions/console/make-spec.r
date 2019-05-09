REBOL []

name: 'Console
source: %console/mod-console.c

includes: [
    %prep/extensions/console  ; for %tmp-mod-console.h
]
requires: 'Event
