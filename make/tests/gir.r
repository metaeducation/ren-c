REBOL []
libc: make library! %libc.so.6
gir: make library! %libgirepository-1.0.so

strlen: make routine! compose [
	[
		s [pointer]
		return: [uint64]
	]
	(libc) "strlen"
]

stringfy: func [
	ptr [integer!]
	/local len s
] [
	len: strlen ptr
	s: make struct! [
		s: [uint8 [len]] ptr
	]
	to string! values-of s
]

g-irepository-default: make routine! compose [
	[
		return: [pointer]
	]
	(gir) "g_irepository_get_default"
]

g-irepository-require: make routine! compose [
	[
		repository [pointer]
		namespace [pointer]
		version [pointer]
		flags [uint32]
		error [pointer]
		return: [pointer]
	]
	(gir) "g_irepository_require"
]

g-irepository-get-c-prefix: make routine! compose [
	[
		repository [pointer]
		namespace [pointer]
		return: [pointer]
	]
	(gir) "g_irepository_get_c_prefix"
]

g-irepository-get-typelib-path: make routine! compose [
	[
		repository [pointer]
		namespace [pointer]
		return: [pointer]
	]
	(gir) "g_irepository_get_typelib_path"
]

g-irepository-find-by-name: make routine! compose [
	[
		repository [pointer]
		namespace [pointer]
		name	[pointer]
		return: [pointer]
	]
	(gir) "g_irepository_find_by_name"
]

g-base-info-get-type: make routine! compose [
	[
		info [pointer]
		return: [int32]
	]
	(gir) "g_base_info_get_type"
]

g-object-info-get-n-methods: make routine! compose [
	[
		info [pointer]
		return: [int32]
	]
	(gir) "g_object_info_get_n_methods"
]

g-object-info-get-method: make routine! compose [
	[
		info [pointer]
		n	[int32]
		return: [pointer]
	]
	(gir) "g_object_info_get_method"
]

g-function-info-get-symbol: make routine! compose [
	[
		info [pointer]
		return: [pointer]
	]
	(gir) "g_function_info_get_symbol"
]

GError: make struct! [
	domain [uint32]
	code [uint32]
	message [pointer]
]

NULL: 0

rep: g-irepository-default

print ["rep:" rep]

gtk: g-irepository-require rep "Gtk" NULL 0 (reflect GError 'addr)

print ["gtk:" gtk]

c-prefix: g-irepository-get-c-prefix rep "Gtk"
print ["prefix:" stringfy c-prefix]

c-path: g-irepository-get-typelib-path rep "Gtk"
print ["path:" stringfy c-path]

info: g-irepository-find-by-name rep "Gtk" "Button"

print ["info:" info]

type: g-base-info-get-type info
print ["type:" type]

if  type = 7 [ ;object
	n_methods: g-object-info-get-n-methods info
	i: 0
	while [i < n_methods] [
		m: g-object-info-get-method info  i
		name: g-function-info-get-symbol m
		print ["method:" stringfy name]
		++ i
	]
]
