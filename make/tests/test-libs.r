REBOL []
recycle/torture
forever [
	libs: make library! %./libs.so
	read-s: make routine! compose [
		[
			a [
				struct! [
					struct! [int32 bi] bs
					int32 [2] i
					int32 j
				]
			]
		]
		(libs) "read_s"
	]

	a: make struct! [
		struct! [int32 bi] bs
		int32 [2] i
		int32 j
	]

	i: 0
	while [i < 100] [
		a/bs/bi: 100 + i
		a/i/1: 200 + i
		a/i/2: 300 + i
		a/j: 400 + i

		read-s a
		++ i
	]

	print ["a:" mold a]

	return-s: make routine! compose/deep [
		[
			i [int32]
			return: [(a)]
		]
		(libs) "return_s"
	]

	i: 0
	print ["i = " i]
	s: return-s i
	print ["s: " s]
	i: 0
	while [i < 100] [
		print ["i = " i]
		s: return-s i
		print ["s:" mold s]
		++ i
	]
	print now
	wait [2]
]
