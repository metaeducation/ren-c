REBOL []

name: 'Gob
source: %gob/mod-gob.c
depends: [
    %gob/t-gob.c
]
includes: [%prep/extensions/gob]
definitions: []
cflags: []
searches: []
ldflags: []

libraries: []

options: []

requires: 'Event  ; uses SYS.MAKE-SCHEME with GET-EVENT-ACTOR-HANDLE (?)
