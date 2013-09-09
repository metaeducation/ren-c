REBOL [
	Title: "Saphir - patch file"
]

;enable console on next print,probe etc.
console-output true

if system/version/4 = 13 [ ;android temp encapper boot code
	use [data][
		if data: get-encap-data [
			do load decode 'text decompress decloak data "droiddebug"
			quit
		]
	]
]
