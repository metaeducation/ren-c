REBOL []
libgtk: try/except [
	make library! %libgtk-3.so
][
	make library! %libgtk-3.so.0
]
libglib: try/except [
	make library! %libglib-2.0.so
][
	make library! %libglib-2.0.so.0
]
libgob: try/except [
	make library! %libgobject-2.0.so
][
	make library! %libgobject-2.0.so.0
]

gtk-init: make routine! compose [
	[
		argc [pointer]
		argv [pointer]
	]
	(libgtk) "gtk_init"
]
gtk-window-new: make routine! compose [
	[
		type [int32]
		return: [pointer]
	]
	(libgtk) "gtk_window_new"
]

gtk-window-set-default-size: make routine! compose [
	[
		windown [pointer]
		width	[int32]
		height	[int32]
		return: [void]
	]
	(libgtk) "gtk_window_set_default_size"
]

gtk-window-set-resizable: make routine! compose [
	[
		window [pointer]
		resizable [int32]
		return: [void]
	]
	(libgtk) "gtk_window_set_resizable"
]

gtk-window-set-title: make routine! compose [
	[
		win [pointer]
		title [pointer]
	]
	(libgtk) "gtk_window_set_title"
]

gtk-widget-show: make routine! compose [
	[
		widget [pointer]
	]
	(libgtk) "gtk_widget_show"
]
gtk-hbox-new: make routine! compose [
	[
		return: [pointer]
	]
	(libgtk) "gtk_hbox_new"
]

gtk-box-pack-start: make routine! compose [
	[
		box [pointer]
		child [pointer]
		expand [uint8]
		fill [uint8]
		padding [uint32]
		return: [pointer]
	]
	(libgtk) "gtk_box_pack_start"
]

gtk-box-set-spacing: make routine! compose [
	[
		box [pointer]
		spacing [int32]
		return: [void]
	]
	(libgtk) "gtk_box_set_spacing"
]

gtk-box-get-spacing: make routine! compose [
	[
		box [pointer]
		return: [int32]
	]
	(libgtk) "gtk_box_get_spacing"
]

gtk-toggle-button-new-with-label: make routine! compose [
	[
		label [pointer]
		return: [pointer]
	]
	(libgtk) "gtk_toggle_button_new_with_label"
]

gtk-font-button-new: make routine! compose [
	[
		return: [pointer]
	]
	(libgtk) "gtk_font_button_new"
]
gtk-font-chooser-widget-new: make routine! compose [
	[
		return: [pointer]
	]
	(libgtk) "gtk_font_chooser_widget_new"
]
gtk-font-chooser-set-font: make routine! compose [
	[
		fontchooser [pointer]
		fontname [pointer]
	]
	(libgtk) "gtk_font_chooser_set_font"
]

gtk-color-button-new: make routine! compose [
	[
		return: [pointer]
	]
	(libgtk) "gtk_color_button_new"
]

gtk-main: make routine! compose [
	[
	]
	(libgtk) "gtk_main"
]
gtk-main-quit: make routine! compose [
	[
	]
	(libgtk) "gtk_main_quit"
]

g-signal-connect-data: make routine! compose [
	[
		instance [pointer]
		detailed-signal [pointer]
		c-handler [pointer]
		data	[pointer]
		destroy-data [pointer]
		connect-flags [int32]
		return: [int64]
	]
	(libgob) "g_signal_connect_data"
]

g-signal-connect: func [
	instance [integer!]
	detailed-signal [integer! string! binary!]
	c-handler [integer!]
	data [integer!]
][
	g-signal-connect-data instance detailed-signal c-handler data 0 0
]

gtk-button-new-with-label: make routine! compose [
	[
		label [pointer]
		return: [pointer]
	]
	(libgtk) "gtk_button_new_with_label"
]

gtk-button-set-label: make routine! compose [
	[
		button [pointer]
		label [pointer]
	]
	(libgtk) "gtk_button_set_label"
]

gtk-container-add: make routine! compose [
	[
		container [pointer]
		elem	  [pointer]
	]
	(libgtk) "gtk_container_add"
]

init-gtk: function [app] [
	arg0: make struct! [
		uint8 [1 + length? app] appn
	]
	change arg0 append to binary! app #{00}

	argv: make struct! [
		pointer [2] args
	]

	print ["assign pointer"]
	argv/args/1: reflect arg0 'addr

	print ["argv:" argv]
	argc: make struct! [
		int32 c: 1
	]

	addr-argv: make struct! [
		pointer addr: (reflect argv 'addr)
	]

	print ["addr-argv: " addr-argv]
	print ["addr of addr-argv: " reflect addr-argv 'addr]

	gtk-init (reflect argc 'addr) (reflect addr-argv 'addr)
	print ["argc:" argc "argv:" argv]
]

mk-cb: func [
	args [block!]
	body [block!]
	/local r-args arg a tmp-func
][
	r-args: copy []

	arg:[
		copy a word! (append r-args a)
		block!
		opt string!
	]
	attr: [
		set-word!
		block! | word!
	]

	parse args [
		opt string!
		some [ arg | attr ]
	]

	print ["args:" mold args]

	tmp-func: function r-args body

	print ["tmp-func:" mold :tmp-func]
	make callback! compose/deep [[(args)] :tmp-func]
]

on-click-callback: mk-cb [
	widget [pointer]
	data   [pointer]
][
	print ["clicked"]
	i: make struct! compose/deep [
		[
			raw-memory: (data)
			raw-size: 4
	 	]
		int32 i
	]
	i/i: i/i + 1
	gtk-button-set-label widget rejoin ["clicked " i/i either i/i = 1 [" time"][" times"]]
]

app-quit-callback: mk-cb [
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
g-signal-connect win "destroy" (reflect :app-quit-callback 'addr) NULL
gtk-window-set-title win "gtk+ from rebol"

hbox: gtk-hbox-new
gtk-box-set-spacing hbox 10

gtk-container-add win hbox

but1: gtk-button-new-with-label "button 1"
gtk-box-pack-start hbox but1 1 1 0

n-clicked: make struct! [int32 i: 0]
g-signal-connect but1 "clicked" (reflect :on-click-callback 'addr) (reflect n-clicked 'addr)

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
