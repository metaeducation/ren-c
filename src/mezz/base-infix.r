REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "Infix operator symbol definitions"
	Rights: {
		Copyright 2012 REBOL Technologies
		REBOL is a trademark of REBOL Technologies
	}
	License: {
		Licensed under the Apache License, Version 2.0.
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Purpose: {
		This defines some infix operators.

		See ops.r for how the "weird" words that have to be done through
		a tricky SET manage to have bindings in the lib context, even
		though they aren't picked up here as SET-WORD!.
	}
]

binary-to-infix: func [
	{Convert a binary function to its infix equivalent}
	value [any-function!]
][
	; SPEC-OF isn't defined yet at this point in the boot...
	func (
		head insert (reflect :value 'spec) <infix>
	)(
		; WORDS-OF isn't defined either...
		compose [
			(:value) (map-each word reflect :value 'words [to get-word! word])
		]

		; Note that this is effectively "compiling in" the function as
		; a direct value, which is kind of interesting...this means that
		; changing ADD won't change the behavior of +
	)
]

+: binary-to-infix :add
-: binary-to-infix :subtract
*: binary-to-infix :multiply

set (pick [/] 1) binary-to-infix :divide
set (pick [//] 1) binary-to-infix :remainder

**: binary-to-infix :power
=: binary-to-infix :equal?
=?: binary-to-infix :same?
==: binary-to-infix :strict-equal?
!=: binary-to-infix :not-equal?

set (pick [<>] 1) binary-to-infix :not-equal?

!==: binary-to-infix :strict-not-equal?

set (pick [<] 1) binary-to-infix :lesser?
set (pick [<=] 1) binary-to-infix :lesser-or-equal?
set (pick [>] 1) binary-to-infix :greater?
set (pick [>=] 1) binary-to-infix :greater-or-equal?

and: binary-to-infix :and?
or: binary-to-infix :or?
xor: binary-to-infix :xor?

and*: binary-to-infix :and~
or+: binary-to-infix :or~
xor-: binary-to-infix :xor~

; !!! C-isms that are unlikely to be kept
&: binary-to-infix :and~
|: binary-to-infix :or~

unset 'binary-to-infix
