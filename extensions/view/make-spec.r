Rebol [
    name: View
    notes: "See %extensions/README.md for the format and fields of this file"
]

use-librebol: 'yes

sources: [mod-view.c]

; The Windows REQUEST-FILE does not introduce any new dependencies.
; REQUEST-DIR depends on OLE32 for CoInitialize() because it is done
; with some weird COM shell API.  Linux of course introduces a GTK
; dependency, so that is not included by default in the core.
;
; For now just enable REQUEST-FILE on Windows if the view module is
; included, because it doesn't bring along any extra dependencies.
;
libraries: switch platform-config.os-base [
    ;
    ; Note: MinGW is case-sensitive, e.g. %Ole32 won't work.
    ;
    'Windows [
        [%ole32 %comdlg32]
    ]

    ; Note: It seemed to help to put this at the beginning of the
    ; compiler and linking command lines:
    ;
    ;     g++ `pkg-config --cflags --libs gtk+-3.0` ...
    ;
    ; You would currently have to define USE_GTK_FILECHOOSER to get
    ; the common dialog code in REQUEST-FILE.
    ;
    'Linux [
        comment [%gtk-3 %gobject-2.0 %glib-2.0]
        []
    ]
]
