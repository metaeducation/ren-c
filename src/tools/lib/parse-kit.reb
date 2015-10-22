; file: https://raw.githubusercontent.com/codebybrett/reb/master/parse-kit.reb
; date: 13-Oct-2015/14:42:43+11:00

REBOL [
	Title: {Parsing Kit}
	Purpose: "A collection of parsing tools."
	File: %parse-kit.r
	Author: "Brett Handley"
	Web: http://www.codeconscious.com
	License: {

		Copyright 2015 Brett Handley

		Licensed under the Apache License, Version 2.0 (the "License");
		you may not use this file except in compliance with the License.
		You may obtain a copy of the License at

			http://www.apache.org/licenses/LICENSE-2.0

		Unless required by applicable law or agreed to in writing, software
		distributed under the License is distributed on an "AS IS" BASIS,
		WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
		See the License for the specific language governing permissions and
		limitations under the License.
	}
	History: [
		1.0.0 [1-May-2015 "Initial version." "Brett Handley"]
		1.1.0 [27-May-2015 "First release. Should work with Rebol 2 and Rebol 3." "Brett Handley"]
		1.1.1 [12-Jun-2015 "Bugfix: missed /only on root compose for get-parse." "Brett Handley"]
		1.2.0 [15-Jun-2015 "Modified parsing-deep and added parsing-rewrite." "Brett Handley"]
		1.2.1 [17-Jun-2015 "Bugfix: parsing-when, parsing-unless would fail on alternates." "Brett Handley"]
		1.3.0 [17-Jun-2015 "Added parsing-expression. Impose." "Brett Handley"]
		1.4.0 [8-Jul-2015 "Added after." "Brett Handley"]
		1.5.0 [26-Aug-2015 "Added position and length properties to root. Rename ctx to output." "Brett Handley"]
		1.5.1 [31-Aug-2015 "Fix parsing-unless and optimise for Rebol 3." "Brett Handley"]
		1.6.0 [7-Sep-2015 "Add parsing-earliest and parsing-matched." "Brett Handley"]
		1.7.0 [12-Sep-2015 "Optimise parsing-when for Rebol 3." "Brett Handley"]
		1.8.0 [30-Sep-2015 "Add /only and /once to parsing-rewrite." "Brett Handley"]
		1.9.0 [12-Oct-2015 "Modify to use r2r3-future.r (Ren/C future bridge)." "Brett Handley"]
	]
]


; ---------------------------------------------------------------------------------------------------------------------
; Notes:
;
;	parsing-at
;
;		Evaluate a block to determine the next input position.
;		Returns a parse rule.
;
;		Use it to simulate additional parse keywords.
;
;		Aside: The word parameter while not strictly necessary
;		       in the design should simplify user code and reuse.
;
;		Example:
;			parse [3] parsing-at x [if attempt [odd? x/1] [next x]]
;
;
;	parsing-deep
;
;		Recursive parsing to search for a parse rule pattern.
;		Returns a parse rule.
;
;		Example:
;			parse [a [[x]]] parsing-deep ['x]
;
;	parsing-earliest
;
;		Create a rule that finds the minimum matched index position for a list of rules.
;
;	parsing-matched
;
;		Create a rule that evaluates a block of positions, one for each rule in a list.
;
;	parsing-expression
;
;		Replace an expression with it's evaluation.
;		Returns a parse rule.
;
;		Has a simpler replacement algorithm than parsing-rewrite,
;		more suited to template style replacement.
;
;		Example:
;			parse block: [now] parsing-expression 'now
;			block
;			== [17-Jun-2015/14:26:42+10:00]
;
;		See Impose which makes use of this function.
;
;	parsing-rewrite
;
;		Creates a rule which rewrites the input according to Patterns
;		and Productions.  Patterns are parse rules. Productions are
;		compose blocks.
;
;		Example:
;
;			date-rule: parsing-rewrite [
;				['time][(now/time)]
;				['date][(now/date)]
;			]
;
;			block: [{Date is} date {time is } time]
;			parse block date-rule
;			block
;			== ["Date is" 15-Jun-2015 "time is " 18:34:24]
;
;
;	parsing-to, parsing-thru
;
;		TO and THRU behaviour for an arbitrary parse rule pattern.
;		Returns a parse rule.
;
;		Note: The resulting rule is not re-entrant because it has local variables
;		      that are not created if the rule is re-entered.
;
;		Example:
;			parse [a x 1] parsing-thru ['x integer!]
;			parse [2 4 5] parsing-thru parsing-at p [if attempt [odd? p/1] [next p]]
;
;
;	parsing-unless
;
;		Implements a not rule.
;		Does not move input position.
;
;		Note: The resulting rule is not re-entrant because it has local variables
;		      that are not created if the rule is re-entered.
;
;		Example:
;
;			not-x: parsing-unless ['x]
;			not-y: parsing-unless ['y]
;			parse [1] [not-x not-y skip]
;
;	parsing-when
;
;		Implements a simple guard.
;		Does not move input position.
;
;		Note: The resulting rule is not re-entrant because it has local variables
;		      that are not created if the rule is re-entered.
;
;		Example:
;
;			two-ints: parsing-when [2 integer!]
;			parse [1 2] [two-ints 2 skip]
;
;	get-parse
;
;		Returns a tree representing the path PARSE takes through specified parse rules.
;
;		The tree has this structure:
;
;			node: [word parent properties child1 child2 ... childn]
;
;			where the node properties has type, input position and length,
;			and the root node corresponds to the rule argument given to parse.
;
;		It should be straight forward to convert this tree to other structures as necessary.
;		
; 
;	impose
;
;		Reduce expressions to their values within a template block.
;
;		It's a simple convenience function for parsing-expression that lends itself
;		to templating.
;
;	after
;
;		Returns next series position if rule is matched, or none if not.
;
; ---------------------------------------------------------------------------------------------------------------------

script-needs [
	%set-words-of.reb
]


; ----------------------------------------------------------------------
; Rule functions
; ----------------------------------------------------------------------


parsing-at: func [
	{Defines a rule which evaluates a block for the next input position, fails otherwise.}
	'word [word!] {Word set to input position (will be local).}
	block [block!] {Block to evaluate. Return next input position, or none/false.}
	/end {Drop the default tail check (allows evaluation at the tail).}
] [
	use [result position][
		block: to paren! block
		if not end [
			block: compose/deep/only [all [not tail? (word) (block)]]
		]
		block: compose/deep [result: either position: (block) [[:position]][[end skip]]]
		use compose [(word)] compose/deep [
			[(to set-word! :word) (to paren! block) result]
		]
	]
]

parsing-deep: func [
	{Create a rule to search recursively for a pattern with local variables.}
	pattern [block!] {Parse rule.}
	/all {Match every occurrence of pattern, in effect all the input. Default is to stop after first match.}
	/extern words [block! logic! none!] {These words are not local. TRUE = All words are not local.}
	/contains {Test if the first input element contains the pattern.}
	/skip {Rule for next position.} next-position {A parse rule. Default is SKIP.}
	/recurse {Test before recursion.} recursion-guard {A parse rule - must succeed for recursion. Default is to enter any-block!]}
	/local set-words initialise rule recursion
] [

	if system/version > 2.100.0 [; R3
		if not recurse [recursion-guard: [and any-block!]]
	]

	set-words: collect [
		foreach word [pattern next-position recursion-guard] [
			keep map-each word set-words-of/deep any [get word []] [to word! word]
		]
	]

	words: any [
		if same? :words true [set-words]
		[]
	]

	use [
		match-deep advance guard match search position result local-vars bind-vars
	] [

		local-vars: exclude set-words words

		bind-vars: none
		if not empty? local-vars [
			bind-vars: to paren! compose/deep/only [
				use (local-vars) [local-vars: (local-vars)] ; Give locals their own context.
				match: bind/copy match first local-vars ; New rule with it's own locals.
			]
			if next-position [append bind-vars [advance: bind/copy advance first local-vars]]
			if recursion-guard [append bind-vars [guard: bind/copy guard first local-vars]]
		]

		either all [
			match-deep: copy/deep [
				bind-vars
				some [match (result: none) | search]
				result
			]
		][
			match-deep: copy/deep [
				bind-vars
				some [match (match: search: [end skip] result: none) | search]
				result
			]
		]

		initialise: compose/only [
			match: (compose [(:pattern)])
			advance: (either next-position [compose [(:next-position)]][to lit-word! 'skip])
			guard: (if recursion-guard [compose [(:recursion-guard)]])
			search: (copy [guard into match-deep | advance])
			result: [end skip]
		]

		new-line compose [
			(to paren! initialise)
			(either contains [[guard into]][[]]) match-deep
		] true

	]

]

parsing-earliest: funct [
	{Create a rule that parses every TO rule in a list to find the match with lowest index.}
	rules [block!] {Block of rules to pass to TO.}
] [

	use [pos min] [
		parsing-matched list rules [

			remove-each x list [none? x]

			if not empty? list [

				min: index-of pos: list/1
				list: next list
				while [not tail? list] [
					if list/1 [
						if not same? head pos head list/1 [
							fail {Can only compare rule positions for the same series.}
						]
						if lesser? index-of list/1 min [
							min: index-of pos: list/1
						]
					]
					list: next list
				]

				pos
			]
		]
	]
]

parsing-expression: funct [
	{Creates a rule that replaces an expression with it's evaluation.}
	symbol [word! block!] {The word or block of words that denote the expression to be evaluated.}
	/all {Deep replace all expressions.}
	/next evaluate [function!] {A function like DO/next. DO/Next is the default.}
	/stay {Stay at position after replacement.}
	/unset {Unset! is retained in result.}
] [
	use [value rest] [
		rule: funct [x][
			if not word? :x [fail {expression symbol must be a word!}]
			x: to lit-word! :x
			compose/deep/only [(:x) | (parsing-when [path!]) into [(:x) to end]]
		]
		match: remove collect [foreach x compose [(:symbol)] [keep compose [| (rule :x)]]]
		condition: parsing-when match
		evaluation: parsing-at input compose/deep [
			set [value rest] (either next [:evaluate][[do/next]]) input
			(either unset [[]][[unless value? 'value [value: []]]])
			change/part input get/any 'value rest
		]
		rule: compose [
			(condition)
			(evaluation)
		]
	]
	if stay [rule: compose/only [position: (rule) :position]]
	if all [rule: parsing-deep/all rule]
	rule
]

parsing-matched: funct [
	{Create a rule that evaluates a block of positions, one for each rule in a list.}
	'word [word!] {Word set to result positions of the rules (will be local).}
	rules [block!] {Block of rules to match.}
	block [block!] {Block to evaluate. Return next input position, or none/false.}
] [

	word: use reduce [:word] reduce [compose [(word)]]

	use [result positions position start] [
		collect [
			keep compose/only [(to paren! compose [positions: array (length rules)]) start:]
			for i 1 length rules 1 [
				keep compose/deep/only [
					:start (to paren! [position: none]) opt [(:rules/:i) position:] (to paren! compose [poke positions (i) position])
				]
			]
			keep/only to paren! compose/only [
				(to set-word! word/1) positions
				result: either position: (to paren! bind/copy block word/1) [[:position]] [[end skip]]
			]
			keep/only 'result
		]
	]
]

parsing-rewrite: funct [
	{Creates a rule that rewrites the input according to patterns and productions. }
	rules [block!] {Rewriting rules in pairs of Pattern (parse rule) and Production (a compose block).}
	/once {Only one full top down search and replace pass is performed.}
	/only {Replacements are not reprocessed in the same pass.}
] [

	use [rules* prod p1 p2][

		; This code taken from Gabriele's rewrite.r function.
		; Please see http://www.colellachiara.com/soft/Misc/rewrite.r

		rules*: make block! 16
		foreach [pattern production] rules [
			insert insert/only insert/only tail rules* pattern make paren! compose/only [
				prod: compose/deep (production)
			] '|
		]
		remove back tail rules*

		edit: either only [
			quote (p1: change/part p1 prod p2)
		][
			quote (change/part p1 prod p2)
		]

		rule: parsing-deep/all compose/only [p1: rules* p2: (edit) :p1]

		either once [rule][
			compose/only [some (rule)]
		]
	]
]

parsing-thru: func [
	{Creates a rule that performs a THRU on an arbitrary rule.}
	rule [block!] {Parse rule.}
	/skip {Advance position.} next-position {A parse rule. Default is to SKIP.}
] [

	use [match search result][

		initialise: compose/only [
			match: (compose [(:rule)])
			search: (either next-position [compose [(:next-position)]][to lit-word! 'skip])
			result: [end skip]
		]

		new-line compose/only [
			(to paren! initialise)
			some [match (match: search: [end skip] result: none) | search]
			result
		] true

	]

]

parsing-to: funct [
	{Creates a rule that performs a TO on an arbitrary rule.}
	rule [block!] {Parse rule.}
	/skip {Advance position.} next-position {A parse rule. Default is to SKIP.}
] [

	use [position][
		compose [(parsing-thru/skip compose [position: (rule)] :next-position) :position]
	]

]

either system/version > 2.100.0 [; Rebol 3

	parsing-unless: func [
		{Creates a rule that fails if the rule matches, succeeds if the rule fails. Will not consume input. Susperseeded by Rebol 3's NOT.}
		rule [block!] {Parse rule.}
	] [
		compose/only [not (rule)]
	]

	parsing-when: func [
		{Creates a rule that succeeds or fails depending on the pattern but does not move input position.}
		pattern [block!] {Parse pattern.}
	] [
		compose/only [and (pattern)]
	]

] [; Rebol 2

	parsing-unless: func [
		{Creates a rule that fails if the rule matches, succeeds if the rule fails. Will not consume input.}
		rule [block!] {Parse rule.}
		/local new
	] [
		use [position result] [
			new: copy/deep [[position: rule (result: [end skip]) | (result: [:position])] result]
			change/only/part next new/1 rule 1
			new
		]
	]

	parsing-when: func [
		{Creates a rule that succeeds or fails depending on the pattern but does not move input position.}
		pattern [block!] {Parse pattern.}
	] [
		use [position] [
			compose/only [position: (pattern) :position]
		]
	]

]


; ----------------------------------------------------------------------
; Rule manipulation
; ----------------------------------------------------------------------

pre-rule-action: funct [
	{Inserts action at start of the rule.}
	rule
	action {Action called before rule is tested.} [block!]
	/position {Get input position before action.} position-word [word!]
	/modify {Modify input position after successful action.} new-position-word {Can be same as position-word.} [word!]
] [

	set-pos: either position [to set-word! :position-word] [[]]
	get-pos: either modify [to get-word! :new-position-word] [[]]

	def: compose [
		(:set-pos)
		(either empty? action [[]] [to paren! :action])
		(:get-pos)
	]

	append/only pos: tail def :rule
	new-line def true
	new-line pos true
	def
]


post-rule-action: funct [
	{Appends action at end of the rule.}
	rule
	action [block!]
	/alt {Call when rule fails and return failure by default. New rule returns success with /modify.} [block!]
	/position {Get input position before action.} position-word [word!]
	/modify {Modify input position.} new-position-word {Can be same as position-word.} [word!]
] [

	set-pos: either position [to set-word! :position-word] [[]]
	get-pos: either modify [to get-word! :new-position-word] [[]]

	instructions: compose [
		(:set-pos)
		(either empty? action [[]] [to paren! :action])
		(:get-pos)
	]

	if not empty? instructions [
		if alt [
			postact: either modify [[]] [[end skip]]
			instructions: compose [
				|
				(:instructions)
				(:postact)
			]
		]
	]

	append pos: tail def: compose/only [(:rule)] instructions
	new-line pos true
	def

]


; ----------------------------------------------------------------------
; Parse events
; ----------------------------------------------------------------------

on-parsing: funct [
	{Modifies rule to call function for [name matched position] when rule begins (none), succeeds (true) or fails (false).}
	rule [word!] {The rule name.}
	event [function!] {Single block argument callback function.}
	/literal {Evaluates event for [name length starting-position] only when rule succeeds.}
] [

	use [; Variable local to the new rule.
		event.at ; Position variable is named so that restore-rule can identify the rule.
		event.end
		saved-rule
		event
	] [

		saved-rule: reduce [get rule]

		event: get (bind 'event 'rule) ; Use local variable to store function.

		def: get rule

		; TODO: Avoid unnecessary nesting of rule blocks (pre-rule, post-rule return blocks).

		either literal [

			def: pre-rule-action/position :def [] 'event.at

			def: post-rule-action/position :def compose/deep [
				event reduce [(to lit-word! :rule) subtract index-of :event.end index-of :event.at :event.at]
			] 'event.end

		] [

			def: pre-rule-action/position :def compose/deep [
				event reduce [(to lit-word! :rule) none :event.at]
			] 'event.at

			def: post-rule-action/position :def compose/deep [
				event reduce [(to lit-word! :rule) true :event.end]
			] 'event.end

			def: post-rule-action/alt/position :def compose/deep [
				event reduce [(to lit-word! :rule) false :event.end]
			] 'event.end

		]

	]

	set rule def
]


restore-rule: funct [
	{Restores rule modified by on-parsing function to original definition.}
	rule [word!] {The rule name.}
] [

	if all [(block? def: get rule) (found? pos: find/last :def first [event.end:])] [
		set rule first get bind 'saved-rule pos/1
	]

	get rule
]


; ----------------------------------------------------------------------
; Parse trees
; ----------------------------------------------------------------------

get-parse: funct [
	{Returns a PARSE tree for specified rules. Check the result of Parse to determine validity.}
	body [block!] {Invoke Parse on your input.}
	rules [block! object!] {Block of words or object. Each word must identify a Parse rule.}
	/literal {Identify literals (must be constant). Saves memory/faster).} literals [block! object!] {Block of words or object.}
	/terminal {Identify terminals (variable length). Avoids stack usage.} terminals [block! object!] {Block of words or object.}
	/nocomplete {Don't complete rules after early Parse exit (Parse's RETURN keyword), returns current emit position.}
	/error error-state [word!] {Set error-state word if an error occurs. Useful for debugging rules.}
] [

	; ----------------------------------------
	; Initialise.
	; ----------------------------------------

	foreach arg [rules terminals literals] [
		if object? def: get arg [def: bind words-of :def :def]
		set arg any [:def copy []]
	]

	node: context [type: name: length: position: none]
	matched: none

	; ----------------------------------------
	; Embed rules event code into the parse rules.
	; ----------------------------------------

	do-rule-event: func [
		rule.evt
	] bind [

		type: 'rule

		set [name matched position] rule.evt

		either none? matched [

			; output points to tail of parent.
			; Add rule node. Push.
			insert/only output output: reduce [name output reduce ['type type 'position position]]
			output: tail output

		] [

			output: second head output ; Pop. output indexes just completed child.

			either matched [

				length: subtract index-of position index-of output/1/3/position ; Length
				append output/1/3 reduce ['length length]

				output: next output ; Accept tree node.
			] [

				remove output ; Reject tree node.
			]
		]

	] node

	foreach rule rules [
		restore-rule :rule ; In case last run was stopped unexpectedly.
		on-parsing :rule :do-rule-event
	]


	; ----------------------------------------
	; Embed terminals event code into the parse rules.
	; ----------------------------------------

	use [start-position] [

		do-terminal-event: func [
			terminal.evt
		] bind [

			set [name matched position] terminal.evt

			either none? matched [

				start-position: :position

			] [

				if matched [

					length: subtract index-of position index-of start-position ; Length
					position: start-position ; Input position

					output: insert/only output reduce [name output compose/only [type terminal position (position) length (length)]]
				]
			]

		] node

	]

	foreach terminal terminals [
		restore-rule terminal ; In case last run was stopped unexpectedly.
		on-parsing :terminal :do-terminal-event
	]

	; ----------------------------------------
	; Embed literals event code into the parse rules.
	; ----------------------------------------

	do-literal-event: func [
		literal.evt
	] bind [

		set [name length position] literal.evt

		output: insert/only output reduce [name output compose/only [type literal position (position) length (length)]]

	] node

	foreach literal literals [
		restore-rule literal ; In case last run was stopped unexpectedly.
		on-parsing/literal :literal :do-literal-event
	]

	; ----------------------------------------
	; Do the parse.
	; ----------------------------------------

	output: tail compose/only [root (none) (copy [type root position none length none])]
	try-result: none
	if error [set :error-state none]
	if error? set/any 'try-result try [do body] [
		if error [
			set :error-state compose/only [
				tree (output)
			] 
		]
	]

	; If we are not back to root level then parse terminated early (RETURN keyword in Rebol 3).
	; Auto complete the outstanding rules.
	if not nocomplete [
		; Complete the unfinished rules.
		while [block? second node: head output ] [
			do-rule-event reduce [node/1 true node/3/position]
		]
	]

	; ----------------------------------------
	; Cleanup and Return result
	; ----------------------------------------

	foreach arg [rules terminals literals] [
		foreach rule get arg [
			restore-rule rule
		]
	]

	trace-result: compose/only [
		out (output)
	]

	if error? get/any 'try-result [
		if error [
			set :error-state compose [
				error (get :error-state)
				(trace-result)
			]
		]
		do :try-result
	] ; Re-raise errors.

	either nocomplete [trace-result] [head trace-result/out]

]

; ----------------------------------------------------------------------
; Block manipulation
; ----------------------------------------------------------------------

impose: funct [
	{Reduce target expressions to their values within a block.}
	symbol [word! path! block!] {A word or block of words that denote the expressions.}
	block [block! paren!] {Block to modify.}
	/func evaluate [function!] {A function like DO/next. DO/Next is the default.}
	/only {Does not re-evaluate replaced expressions.}
	/unset {Unset! is retained in result.}
][
	rule: copy 'parsing-expression/all
	either func [append rule 'next][evaluate: :do]
	if not only [append rule 'stay]
	if unset [append rule 'unset]
	parse block do rule :symbol :evaluate
	block
]

; ----------------------------------------------------------------------
; Other
; ----------------------------------------------------------------------

after: funct [
	{Return next input position if rule matches or none if unmatched.}
	rule {Parse rule to match.}
	input
][
	parse/all/case input compose [(:rule) position:]
	position
]
