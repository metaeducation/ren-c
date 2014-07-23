REBOL []

msvcrt: make library! %msvcrt.dll
getdrives: make routine! compose/deep [
	[return: [uint32]]
	(msvcrt) "_getdrives"
]

maps: getdrives
i: 0
while [i < 26] [
	unless zero? maps and shift 1 i [
		print rejoin [to char! (to integer! #"A") + i ":"]
	]
	++ i
]
close msvcrt
