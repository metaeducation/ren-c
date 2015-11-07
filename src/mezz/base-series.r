REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "REBOL 3 Boot Base: Series Functions"
	Rights: {
		Copyright 2012 REBOL Technologies
		REBOL is a trademark of REBOL Technologies
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Note: {
		This code is evaluated just after actions, natives, sysobj, and other lower
		levels definitions. This file intializes a minimal working environment
		that is used for the rest of the boot.
	}
]


; !!! These used to be series natives that leveraged their implementation
; as a hacked-up re-dispatch to A_PICK.  The method that used was not viable
; when the call stack got its own data structure.  Given that dispatch was
; not itself free, the idea of needing to write such helpers as natives
; "for performance" suggests a faster substitution "macro" construct may
; be required.  Until then, they are mezzanine.

first: func [
	{Returns the first value of a series.}
	value
] [
	pick value 1
]

first+: func [
	{Return the FIRST of a series then increment the series index.}
	'word [word!] "Word must refer to a series"
	/local prior
][
	also (pick prior: get word 1) (set word next prior)
]

second: func [
	{Returns the second value of a series.}
	value
] [
	pick value 2
]

third: func [
	{Returns the third value of a series.}
	value
] [
	pick value 3
]

fourth: func [
	{Returns the fourth value of a series.}
	value
] [
	pick value 4
]

fifth: func [
	{Returns the fifth value of a series.}
	value
] [
	pick value 5
]

sixth: func [
	{Returns the sixth value of a series.}
	value
] [
	pick value 6
]

seventh: func [
	{Returns the seventh value of a series.}
	value
] [
	pick value 7
]

eighth: func [
	{Returns the eighth value of a series.}
	value
] [
	pick value 8
]

ninth: func [
	{Returns the ninth value of a series.}
	value
] [
	pick value 9
]

tenth: func [
	{Returns the tenth value of a series.}
	value
] [
	pick value 10
]

last: func [
	{Returns the last value of a series.}
	value [any-series! tuple! gob!]
	/local len
] [
	case [
		any-series? value [pick back tail value 1]
		tuple? value [pick value length value]
		gob? value [
			; The C code effectively used 'pick value t' with:
			;
			; t = GOB_PANE(VAL_GOB(val)) ? GOB_TAIL(VAL_GOB(val)) : 0;
			; VAL_GOB_INDEX(val) = 0;
			;
			; Try getting same result with what series does.  :-/

			pick back tail value 1
		]
		true [
			; C code said "let the action throw the error", but by virtue
			; of type checking this case should not happen.
			pick value 0
		]
	]
]

;
; !!! End of functions that used to be natives, now mezzanine
;


repend: func [
	"Appends a reduced value to a series and returns the series head."
	series [any-series! port! map! gob! object! bitset!] {Series at point to insert (modified)}
	value {The value to insert}
	/part {Limits to a given length or position}
	limit [any-number! any-series! pair!]
	/only {Inserts a series as a series}
	/dup {Duplicates the insert a specified number of times}
	count [any-number! pair!]
][
	apply :append [series reduce :value part limit only dup count]
]

join: func [
	"Concatenates values."
	value "Base value"
	rest "Value or block of values"
][
	value: either any-series? :value [copy value] [form :value]
	repend value :rest
]

reform: func [
	"Forms a reduced block and returns a string."
	value "Value to reduce and form"
	;/with "separator"
][
	form reduce :value
]
