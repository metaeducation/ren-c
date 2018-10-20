REBOL []

recycle/torture

libgtk: trap/with [
    make library! %libgtk-3.so
][
    make library! %libgtk-3.so.0
]
libglib: trap/with [
    make library! %libglib-2.0.so
][
    make library! %libglib-2.0.so.0
]
libgob: trap/with [
    make library! %libgobject-2.0.so
][
    make library! %libgobject-2.0.so.0
]

gtk-init:
    make-routine libgtk "gtk_init" [
        argc [pointer]
        argv [pointer]
    ]

gtk-window-new:
    make-routine libgtk "gtk_window_new" [
        type [int32]
        return: [pointer]
    ]

gtk-window-set-default-size:
    make-routine libgtk "gtk_window_set_default_size" [
        windown [pointer]
        width    [int32]
        height    [int32]
        return: [void]
    ]

gtk-window-set-resizable:
    make-routine libgtk "gtk_window_set_resizable" [
        window [pointer]
        resizable [int32]
        return: [void]
    ]

gtk-window-set-title:
    make-routine libgtk "gtk_window_set_title" [
        win [pointer]
        title [pointer]
    ]

gtk-widget-show:
    make-routine libgtk "gtk_widget_show" [
        widget [pointer]
    ]

gtk-hbox-new:
    make-routine libgtk "gtk_hbox_new" [
        return: [pointer]
    ]

gtk-box-pack-start:
    make-routine libgtk "gtk_box_pack_start" [
        box [pointer]
        child [pointer]
        expand [uint8]
        fill [uint8]
        padding [uint32]
        return: [pointer]
    ]

gtk-box-set-spacing:
    make-routine libgtk "gtk_box_set_spacing" [
        box [pointer]
        spacing [int32]
        return: [void]
    ]

gtk-box-get-spacing:
    make-routine libgtk "gtk_box_get_spacing" [
        box [pointer]
        return: [int32]
    ]

gtk-toggle-button-new-with-label:
    make-routine libgtk "gtk_toggle_button_new_with_label" [
        label [pointer]
        return: [pointer]
    ]

gtk-font-button-new:
    make-routine libgtk "gtk_font_button_new" [
        return: [pointer]
    ]

gtk-font-chooser-widget-new:
    make-routine libgtk "gtk_font_chooser_widget_new" [
        return: [pointer]
    ]

gtk-font-chooser-set-font:
    make-routine libgtk "gtk_font_chooser_set_font" [
        fontchooser [pointer]
        fontname [pointer]
    ]

gtk-color-button-new:
    make-routine libgtk "gtk_color_button_new" [
        return: [pointer]
    ]

gtk-main:
    make-routine libgtk "gtk_main" []

gtk-main-quit:
    make-routine libgtk "gtk_main_quit" []

g-signal-connect-data:
    make-routine libgob "g_signal_connect_data" [
        instance [pointer]
        detailed-signal [pointer]
        c-handler [pointer]
        data    [pointer]
        destroy-data [pointer]
        connect-flags [int32]
        return: [int64]
    ]

g-signal-connect: func [
    instance [integer!]
    detailed-signal [integer! text! binary!]
    c-handler [action!]
    data [integer!]
][
    g-signal-connect-data instance detailed-signal :c-handler data 0 0
]

gtk-button-new-with-label:
    make-routine libgtk "gtk_button_new_with_label" [
        label [pointer]
        return: [pointer]
    ]

gtk-button-set-label:
    make-routine libgtk "gtk_button_set_label" [
        button [pointer]
        label [pointer]
    ]

gtk-container-add:
    make-routine libgtk "gtk_container_add" [
        container [pointer]
        elem      [pointer]
    ]

init-gtk: function [app] [
    arg0: make struct! compose/deep [
        appn [uint8 [(1 + length of app)]]
    ]
    change arg0 append to binary! app #{00}

    argv: make struct! [
        args [pointer [2]]
    ]

    print ["assign pointer"]
    argv/args/1: addr-of arg0

    print ["argv:" argv]
    argc: make struct! [
        c: [int32] 1
    ]

    addr-argv: make struct! [
        addr: [pointer] (addr-of argv)
    ]

    print ["addr-argv:" addr-argv]
    print ["addr of addr-argv:" addr-of addr-argv]

    gtk-init (addr-of argc) (addr-of addr-argv)
    print ["argc:" argc "argv:" argv]
]

on-click-callback: make-callback [
    widget [pointer]
    data   [pointer]
][
    print ["clicked"]
    i: make struct! compose/deep [
        [
            raw-memory: (data)
            raw-size: 4
         ]
        i [int32]
    ]
    i/i: i/i + 1
    gtk-button-set-label widget spaced [
        "clicked" i/i either i/i == 1 ["time"]["times"]
    ]
]

app-quit-callback: make-callback [
][
    print ["app quiting"]
    gtk-main-quit
]

NULL: 0
GTK_WINDOW_TOPLEVEL: 0
GTK_WINDOW_POPUP: 1

init-gtk "./r3-view-linux"
print ["gtk initialized"]

win: gtk-window-new GTK_WINDOW_TOPLEVEL
gtk-window-set-default-size win 10 10
gtk-window-set-resizable win 1
print ["win:" win]
g-signal-connect win "destroy" :app-quit-callback NULL
gtk-window-set-title win "gtk+ from rebol"

hbox: gtk-hbox-new
gtk-box-set-spacing hbox 10

gtk-container-add win hbox

but1: gtk-button-new-with-label "button 1"
gtk-box-pack-start hbox but1 1 1 0

n-clicked: make struct! [i: [int32] 0]
g-signal-connect but1 "clicked" :on-click-callback (addr-of n-clicked)

but2: gtk-button-new-with-label "button 2"
gtk-box-pack-start hbox but2 1 1 0

but3: gtk-toggle-button-new-with-label "toggle"
gtk-box-pack-start hbox but3 1 1 0

;font-chooser: gtk-font-chooser-widget-new
;gtk-box-pack-start hbox font-chooser 1 1 0
;gtk-font-chooser-set-font font-chooser "Times Bold 18"

font-button: gtk-font-button-new
gtk-box-pack-start hbox font-button 1 1 0

color-button: gtk-color-button-new
gtk-box-pack-start hbox color-button 1 1 0

gtk-widget-show color-button
gtk-widget-show font-button
gtk-widget-show but1
gtk-widget-show but2
gtk-widget-show but3
gtk-widget-show hbox
gtk-widget-show win
print ["spacing:" gtk-box-get-spacing hbox]
gtk-main
