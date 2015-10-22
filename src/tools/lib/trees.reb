; file: https://raw.githubusercontent.com/codebybrett/reb/master/trees.reb
; date: 13-Oct-2015/15:46:07+11:00

REBOL [
	Title: "Trees"
	Version: 1.0.0
	Rights: {
		Copyright 2015 Brett Handley
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
	Purpose: {Functions to work upon trees.}
]

; ---------------------------------------------------------------------------------------------------------------------
;
; WORK IN PROGRESS - NEEDS REVIEW
;
; Each tree node has structure:
;
; 	[value parent properties child1 child2 ...]
;
;	Child nodes that are leaves could be any type - to allow some flexibility.
;
; ---------------------------------------------------------------------------------------------------------------------

visit-tree: func [
	{Evaluates a tree. RECURSE and MAP-NODES recursively process child nodes.}
	tree [block!] {[value prop child1 child2 ...].}
	'arg [word! block!] {Word set to each node (local). Can be FUNCT spec. Optional second argument is accumulator.}
	body [block!] {Block to evaluate. Call VISIT on each child node to visit. Set-words are local by default.}
	/initial value {Initial value.}
	/extern other-words [block!] {These words are not local.}
] [

	arg: compose [(:arg)]
	word: remove-each x copy arg [not word? x]

	use [visit] [

		visit: funct arg compose/deep [

			map-nodes: func [/count {Map first n nodes.} n [integer!]][
				either count [
					map-each item copy/part at node 4 n [visit item]
				][
					map-each item at node 4 [visit item]
				]
			]
			recurse: func [][foreach item at node 4 [visit item]]

			(bind/copy body 'visit)

		] any [other-words []]

		visit tree ; value
	]
]

prettify-tree: funct [
	{Make the tree pretty.}
	tree [block!] {[value properties child1 child2 ...]}
][

	visit-tree tree node [

		new-line/all at node 4 true
		recurse
	]

	tree
]

update-parents: funct [
	{Updates tree parent references.}
	tree [block!] {[value properties child1 child2 ...]}
	/parent node [none! block!]
][

	reference: at tree 4
	forall reference [
		if block? reference/1 [
			update-parents/parent reference/1 reference
		]
	]

	tree
]

remove-parents: funct [
	{Remove parents from a tree [node-type parent properties child1 child2 ...].}
	tree [block!]
][

	foreach node at tree 4 [
		if block? node [remove-parents node]
	]
	remove at tree 2

	tree
]

add-parents: funct [
	{Modify structure [value properties child1 child2 ...] to restore parents to a tree.}
	block [block!]
	/parent node [none! block!] {Specify parent node.}
][

	insert/only at block 2 node

	reference: at block 4
	forall reference [
		if block? reference/1 [
			add-parents/parent reference/1 reference
		]
	]

	block
]

cut-child: funct [
	{Replaces node at position with it's children.}
	position [block!] {[[value prop child1 child2 ...] ...]}
][

	children: at position/1 4
	insert remove position children

	position
]

; TODO: Not yet tried.
cut-each: funct [
	{Replaces each child that meet condition with it's children.}
	'word [word!] {Set to state each time.}
	position [block!] {[[value prop child1 child2 ...] ...]}
	condition [block!] {Evaluate to True to cut the node.}
][

	state: compose/only [index: count: 0 position: (position)]

	use reduce [word] [

		evaluate: func [/local children] compose bind [

			set word state
			index: index + 1
			either (bind to paren! condition word) [
				count: count + 1
				children: at position/1 4
				insert remove position children
			][next position]

		] state

		while [not tail? state/position][
			position: evaluate
		]
	]

	if state/count > 0 [update-parents head position]
	position
]

; TODO: Need review and a better name.
; Think through this and related simple operations.

cut-tree: funct [
	{Replaces node with its children if condition is satisfied.}
	tree [block!] {A block of trees [value prop child1 child2 ...].}
	condition [block!] {Child POSITION in parent is bound.}
] [

	visit-tree t node compose/deep [

		position: at node 4
		while [not tail? position] [
			child: position/1
			either (to paren! condition) [
				cut-node position
			] [
				position: next position
			]
		]

		recurse
	]
]

; TODO: Review. Possibly should not be part of this file (should handle get-parse literals optimially)
using-tree-content: funct [
	{Replace properties position and length with contents.}
	tree [block!] {A block of trees [value prop child1 child2 ...].}
] [

	visit-tree tree node [

		if empty? at node 4 [
			content: copy/part node/3/position node/3/length
			append node/3 compose/only [content (content)]
		]
		remove/part find node/3 'position 2
		remove/part find node/3 'length 2

		recurse
	]

	tree
]


; TODO: Change to tree. Get working. Use update-parents.
infill-trees: funct [
	{Fills content gaps with new nodes. Nodes require properties position [series!] and length [integer!].}
	trees [block!] {[value properties child1 child2 ...] ...}
	word [word!] {Name of the filler tree nodes.}
] [

	infill-node: func [node /local children child fill-node length] [

		children: at node 4

		while [not tail? children] [

			child: first children
			start: children/1/3/position

			if all [
				same? head position head start ; Might be different blocks.
				positive? length: subtract index-of start index-of position
			] [
				fill-node: compose/only [
					(word) none (
						compose/only [position (position) length (length)]
					)
				]
				children: insert/only children new-line/all fill-node false
			]
			position: start

			infill-node child
			children: next children
		]

		exit
	]


	if not empty? trees [

		position: trees/1/2/position
		foreach tree trees [infill-node tree]
	]

	trees
]
