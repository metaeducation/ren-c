REBOL [
	Title: "R3 Encap boot code"
]

;make sure printing is off
console-output false

use [decrypt err f s d b c e k][
	decrypt: funct [
		data [string! binary!]
	][
		out: copy #{}
		random/seed "REBOL" 
		foreach c data [
			append out c - random 100
		] 
		to-string debase/base reverse out 64
	]

	if error? err: try [

			f: open/read/seek system/options/boot
			s: length? f
			d: copy skip f s - 9
			b: to integer! next d
			c: d/1
			d: copy/part skip head f b e: s - b - 9 - c
			k: decrypt copy/part skip head f b + e c
			d: decode 'text decompress decloak d k
			close f
			do load d
	][
		show-console
		print form err
		error? try [ask "** Press enter to quit..."]
	]
]

quit



