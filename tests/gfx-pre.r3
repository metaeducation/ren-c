REBOL [
	title: "REBOL Graphics proprocessors for DRAW, SHAPE and TEXT dialects"
	author: "Richard 'Cyphre' Smolak"
	version: 0.0.14
]

context [

	ext-shape: import 'shape
	ext-draw: import 'draw
	ext-text: import 'text

	lit-word: false ;flag for lit-word! detection
	any-arg?: false ;turns 'any number of arg' mode on/off

	;----------------------------------------
	;command and argument syntax definitions for dialects
	;----------------------------------------
	
	text-args: make object! [
		b: bold: [logic!]
		i: italic: [logic!]
		u: underline: [logic!]
		font: [object!]
		para: [object!]
		size: [integer!]
		shadow: [pair! | tuple! | number!]
		scroll: [pair!]
		drop: [integer!]
		anti-alias: [logic!]
		nl: newline: none
		caret: [object!]
		center: none
		left: none
		right: none
	]

	shape-args: make object! [
		arc: [
			pair!
			| pair!
			| number!
			| 'sweep
			| 'large
		]
		close: none
		curv: [pair! (any-arg?: true)]
		curve: [pair! (any-arg?: true)]	
		hline: [number!]
		line: [pair! (any-arg?: true)]
		move: [pair!]
		qcurv: [pair!]
		qcurve: [pair! (any-arg?: true)]
		vline: [number!]
	]

	draw-args: make object! [
		anti-alias: [logic!]
		arc: [
			pair!
			| pair!
			| number!
			| number!
			| 'closed
		]
		arrow: [tuple! | pair!]
		box: [pair! | pair! | number!]
		curve: [pair! (any-arg?: true)]
		circle: [pair! | number! | number!]
		clip: [pair! | pair! | logic!]
;		effect: [pair! | pair! | block!]
		ellipse: [pair! | pair!]
		fill-pen: [tuple! | image! | logic!]
		fill-rule: ['even-odd | 'non-zero]
		gamma: [number!]
		grad-pen: [
			'conic
			| 'cubic
			| 'diagonal
			| 'diamond
			| 'linear
			| 'radial
			| 'normal
			| 'repeat
			| 'reflect
			| pair!
;			| pair!
			| pair!
			| number!
			| number!
			| number!
			| logic!
			| block!
		]
		invert-matrix: none
		image: [
			image! | 
			pair! (any-arg?: true)
		]
		image-filter: [
			'nearest
			| 'bilinear
			| 'bicubic
			| 'gaussian
			| 'resize
			| 'resample
			| number!
		]
		image-options: [
			tuple! | 'border | 'no-border
		]
		image-pattern: [
			'normal | 'repeat | 'reflect | pair! | pair!
		]
		line: [pair! (any-arg?: true)]
		line-cap: ['butt | 'square	| 'rounded]
		line-join: ['miter | 'miter-bevel | 'round | 'bevel]
		line-pattern: [logic! | tuple! | number! (any-arg?: true)]
		line-width: [number! | 'fixed]
		matrix: [block!]
		pen: [tuple! | image! | logic!]
		polygon: [pair! (any-arg?: true)]
		push: [block!]
		reset-matrix: none
		rotate: [number!]
		scale: [pair! | number! | number!]
		shape: [block!]
		skew: [pair!]
		spline: [integer! | 'closed | pair! (any-arg?: true)]
		text: ['aliased | 'antialiased | 'vectorial | pair! | pair! | block!]
		transform: [number! | number! | number! | pair! | pair!]
		translate: [pair!]
		triangle: [pair! | pair! | pair! | tuple! | tuple! | tuple! | number!]
	]

	;-------------------------------------------------------------------------------------
	;sets of words that are not evaluated during the initial REDUCE on input dialect block
	;-------------------------------------------------------------------------------------
	
	text-words: words-of text-args

	shape-words: append words-of shape-args [
		sweep
		large
	]

	draw-words: append words-of draw-args [
		;fill-rule
		even-odd
		non-zero

		;grad-pen
		conic
		cubic
		diagonal
		diamond
		linear
		radial

		;grad-pen & image-filter
		normal
		repeat
		reflect
		
		;line-cap
		butt
		square
		rounded
		
		;line-join
		miter
		miter-bevel
		round
		bevel
		
		;line-width
		fixed
		
		;arc
		closed
		
		;image-options
		no-border
		border
		
		;image-filter
		nearest
		bilinear
		bicubic
		gaussian
		resize
		resample
		
		;text
		aliased
		antialiased
		vectorial
	]

	;------------------------------------------------
	;rules for allowed commands in the input dialects
	;------------------------------------------------
	
	text-command: [
		string!
		| tuple!
		| char!
		| 'anti-alias
		| 'b
		| 'bold
		| 'caret
		| 'center
		| 'drop
		| 'font
		| 'i
		| 'italic
		| 'left
		| 'nl
		| 'newline
		| 'para
		| 'right
		| 'scroll
		| 'shadow
		| 'size
		| 'u
		| 'underline
	]

	shape-command: [
		(lit-word: false)
		'arc
		| 'close
		| 'curv
		| 'curve
		| 'hline
		| 'line
		| 'move
		| 'qcurv
		| 'qcurve
		| 'vline
		| lit-word! (lit-word: true)
	]

	draw-command: [
		'anti-alias
		| 'arc
		| 'arrow
		| 'box
		| 'circle
		| 'clip
		| 'curve
;		| 'effect
		| 'ellipse
		| 'fill-pen
		| 'fill-rule
		| 'gamma
		| 'grad-pen
		| 'invert-matrix
		| 'image
		| 'image-filter
		| 'image-options
		| 'image-pattern
		| 'line
		| 'line-cap
		| 'line-join
		| 'line-pattern
		| 'line-width
		| 'matrix
		| 'pen
		| 'polygon
		| 'push
		| 'reset-matrix
		| 'rotate
		| 'scale
		| 'shape
		| 'skew
		| 'spline
		| 'text
		| 'transform
		| 'translate
		| 'triangle
	]
	
	;----------------------------------------------------------------
	; prototypes used for argument storage suring the command parsing
	;----------------------------------------------------------------
	
	text-types: make object! [
		logic!: []
		tuple!: []
		integer!: []
		object!: []
		pair!: []
	]

	shape-types: make object! [
		pair!: []
		integer!: []
		decimal!: []
		word!: []
	]

	draw-types: make object! [
		logic!: []
		pair!: []
		integer!: []
		decimal!: []
		tuple!: []
		word!: []
		block!: []
		image!: []
	]

	set 'to-text func [
		src [block!]
		dst [block!]
		/local
			cmd args a text-arg t
	] [
		parse reduce/only src text-words [
			some [
				set cmd text-command (
					args: make text-types []
					text-arg: either word? cmd [
						all [
							text-args/(cmd)
							copy text-args/(cmd)
						]
					][
						none
					]
				)
				any [
					set a text-arg (
						all [
							not none? a
							append select args t: type?/word a a
							remove/part find text-arg t 2
						]
					)
				]
				(
					append dst reduce switch/default type?/word cmd [
						string! [
							['text cmd]
						]
						tuple! [
							['color cmd]
						]
						string! [
							['text to string! cmd]
						]
					][
						append dst reduce switch/default cmd [
							anti-alias [
								['anti-alias either args/logic!/1 = none [true][args/logic!/1]]
							]
							b bold [
								['bold either args/logic!/1 = none [true][args/logic!/1]]
							]
							caret [
								['caret args/object!/1]
							]
							center [
								['center]
							]
							drop [
								['drop any [args/integer!/1 1]]
							]
							font [
								['font args/object!/1]
							]
							i italic [
								['italic either args/logic!/1 = none [true][args/logic!/1]]
							]
							left [
								['left]
							]
							nl newline [
								['text to string! newline]
							]
							para [
								['para args/object!/1]
							]
							right [
								['right]
							]
							scroll [
								['scroll args/pair!/1]
							]
							shadow [
								['shadow args/pair!/1 args/tuple!/1 args/integer!/1]
							]
							size [
								['size args/integer!/1]
							]
							u underline [
								['underline either args/logic!/1 = none [true][args/logic!/1]]
							]
						][[]]
						[]
					]
				)
				| end
				| a: (
					do make error! reform ["TO-TEXT - syntax error at:" copy/part mold/only at src index? a 50  "..."]
				)				
			]
		]
		bind/only dst ext-text
	]

	to-shape: func [
		src [block!]
		dst [block!]
		/local
			cmd args a shape-arg t
	] [
		parse reduce/only src shape-words [
			some [
				set cmd shape-command (
					args: make shape-types []
					shape-arg: either word? cmd [
						all [
							shape-args/(cmd)
							copy shape-args/(cmd)
						]
					][
						none
					]
				)
				any [
					(any-arg?: false) set a shape-arg (
						all [
							not none? a
							append select args t: type?/word a a
							unless any-arg? [
								remove/part find shape-arg t 2
							]
						]
					)
				]
				(					
					append dst reduce switch/default cmd [
						arc [
							[
								either lit-word ['arc']['arc]
								args/pair!/1
								args/pair!/2
								any [args/integer!/1 args/decimal!/1]
								any [all [args/word!/1 = 'sweep 'positive] 'negative]
								any [args/word!/2 all [args/word!/1 = 'large 'large] 'small]
							]
						]
						close [
							['close]
						]
						curv [
							[either lit-word ['curv']['curv] args/pair!]
						]
						curve [
							[either lit-word ['curve']['curve] args/pair!]
						]
						hline [
							[either lit-word ['hline']['hline] any [args/integer!/1 args/decimal!/1]]
						]
						line [
							[either lit-word ['line']['line] any [all [args/pair!/2 args/pair!] args/pair!/1]]
						]
						move [
							[either lit-word ['move']['move] args/pair!/1]
						]
						qcurve [
							[either lit-word ['qcurve']['qcurve] args/pair!]
						]
						qcurv [
							[either lit-word ['qcurv']['qcurv] args/pair!/1]
						]
						vline [
							[either lit-word ['vline']['vline] any [args/integer!/1 args/decimal!/1]]
						]
					][[]]
				)
				| end
				| a: (
					do make error! reform ["TO-SHAPE - syntax error at:" copy/part mold/only at src index? a 50  "..."]
				)				
			]
		]
		bind/only dst ext-shape
	]

	set 'to-draw func [
		src [block!]
		dst [block!]
		/local
			cmd args a t draw-arg
	] [
		parse reduce/only src draw-words [
			some [
				set cmd draw-command (
					args: make draw-types []
					draw-arg: either word? cmd [
						all [
							draw-args/(cmd)
							copy draw-args/(cmd)
						]
					][
						none
					]
					all [draw-arg append draw-arg [| none! (any-arg?: true)]]
				)
				any [
					(any-arg?: false) set a draw-arg (
						all [
							not none? a
							append select args t: type?/word a a
							unless any-arg? [
								remove/part find draw-arg t 2
							]
						]
					)
				]
				(
					append dst reduce switch/default cmd [
						anti-alias [
							['anti-alias args/logic!/1]
						]
						arc [
							[
								'arc
								args/pair!/1
								args/pair!/2
								any [args/integer!/1 args/decimal!/1 0.0]
								any [args/integer!/2 args/decimal!/2 90.0]
								any [args/word!/1 'closed]
							]
						]
						arrow [
							['arrow args/pair!/1 args/tuple!/1]
						]
						box [
							['box args/pair!/1 any [args/pair!/2 100x100] any [args/integer!/1 args/decimal!/1 0]]
						]
						circle [
							[
								'circle 
								any [args/pair!/1 50x50] 
								as-pair a: any [args/integer!/1 args/decimal!/1 50] any [args/integer!/2 args/decimal!/2 a]
							]
						]
						clip [
							either args/logic!/1 = false [
								['clip 0x0 10000x10000] ;should be gob/size
							][
								['clip args/pair!/1 args/pair!/2]
							]
						]
						curve [
							['curve args/pair!/1 args/pair!/2 args/pair!/3 args/pair!/4]
						]
						ellipse [
							['ellipse args/pair!/1 args/pair!/2]
						]
						fill-pen [
							['fill-pen either a: any [args/tuple!/1 args/image!/1 args/logic!/1][a][false]]
						]
						fill-rule [
							['fill-rule args/word!/1]
						]
						gamma [
							['gamma any [args/integer!/1 args/decimal!/1]]
						]
						grad-pen [
							[
								'grad-pen
								any [args/word!/1 'linear]
								any [args/word!/2 'normal]
								any [args/pair!/1 0x0]
;								any [args/pair!/2 0x100]
								as-pair any [args/integer!/1 args/decimal!/1 0] any [args/integer!/2 args/decimal!/2 100]
								any [args/integer!/3 args/decimal!/3 0]
								any [args/pair!/3 1x1]
								args/block!
							]
						]
						image [
							['image args/image!/1 any [all [args/pair!/2 args/pair!] any [args/pair!/1 0x0]]]
						]
						image-filter [
							[
								'image-filter 
								any [args/word!/1 'nearest]
								any [args/word!/2 'resize]
								any [args/integer!/1 args/decimal!/1]
							]
						]
						image-options [
							['image-options args/tuple!/1 any [args/word!/1 'no-border]]
						]
						image-pattern [
							[
								'image-pattern any [args/word!/1 'normal] any [args/pair!/1 0x0] any [args/pair!/2 0x0]
							]
						]
						line [
							['line args/pair!]
						]
						line-cap [
							['line-cap args/word!/1]
						]
						line-join [
							['line-join args/word!/1]
						]
						line-pattern [
							['line-pattern args/tuple!/1 any [args/decimal! args/integer!]]
						]
						line-width [
							['line-width any [args/integer!/1 args/decimal!/1 1] any [args/word!/1 'variable]]
						]
						invert-matrix [
							['invert-matrix]
						]
						matrix [
							['matrix args/block!/1]
						]
						pen [
							['pen either a: any [args/tuple!/1 args/image!/1 args/logic!/1][a][false]]
						]
						polygon [
							['polygon args/pair!]
						]
						push [
							['push to-draw args/block! copy []]
						]
						reset-matrix [
							['reset-matrix]
						]
						rotate [
							['rotate any [args/integer!/1 args/decimal!/1]]
						]
						scale [
							['scale any [args/pair!/1 as-pair any [args/integer!/1 args/decimal!/1] any [args/integer!/2 args/decimal!/2]]]
						]
						shape [
							['shape to-shape args/block! copy []]
						]
						skew [
							['skew args/pair!/1]
						]
						spline [
							['spline args/pair! any [args/integer!/1 0] any [args/word!/1 'opened]]
						]
						text [
							[
								'text 
								any [args/pair!/1 0x0] 
								args/pair!/2
								any [args/word!/1 'raster]
								to-text args/block! copy []
							]
						]
						transform [
							[
								'transform
								any [args/integer!/1 args/decimal!/1 0]
								any [args/pair!/1 0x0]
								any [as-pair a: any [args/integer!/2 args/decimal!/2 1] any [args/integer!/3 args/decimal!/3 a]]
								any [args/pair!/2 0x0]
							]
						]
						translate [
							['translate args/pair!/1]
						]
						triangle [
							[
								'triangle
								args/pair!/1
								any [args/pair!/2 100x100]
								any [args/pair!/3 as-pair args/pair!/1/x any [args/pair!/2/y 100]]
								args/tuple!/1
								args/tuple!/2
								args/tuple!/3
								any [args/integer!/1 args/decimal!/1 0]
							]
						]
					][[]]
				)
				| end
				| a: (
					do make error! reform ["TO-DRAW - syntax error at:" copy/part mold/only at src index? a 50  "..."]
				)				
			]
		]
		bind/only dst ext-draw
	]
]
