REBOL[]

args: parse system/script/args ""
exe: none
payload: none
output: none
windows?: 3 = fourth system/version

while [not tail? args] [
	arg: first args
	case [
		any [arg = "/rebol"
		     arg = "/r"] [
				 exe: second args
				 args: next args
		]
		any [arg = "/payload"
		     arg = "/p"] [
				 payload: second args
				 args: next args
		]
		any [arg = "/payload"
		     arg = "/p"] [
				 payload: second args
				 args: next args
		]
		any [arg = "/output"
		     arg = "/o"] [
				 output: second args
				 args: next args
		]
	]
	args: next args
]

if any [none? exe
	none? payload
	none? output][
	print ["pack.r"]
	print ["^-/rebol | /r^- path-to-rebol"]
	print ["^-/payload | /p^- path-to-payload"]
	print ["^-/output | /o^- path-to-output"]
	quit
]

payload-data: join #{01000000} compress read to file! payload
tmp: join payload ".tmp"
write to file! tmp payload-data

either windows? [
	unicodify: function [
		s [string!]
	][
		ret: copy #{}
		foreach c s [
			append ret join to binary! c #{00}
		]
		join ret #{0000} ;NULL terminator
	]

	kernel32: make library! %kernel32

	print ["kernel:" mold kernel32]
	BeginUpdateResource: make routine! compose [[
		filename [pointer]
		delete-existing-resources [int32]
		return: [pointer]
	] (kernel32) "BeginUpdateResourceW"]

	UpdateResource: make routine! compose [[
		hUpdate [pointer]
		lpType [pointer]
		lpName [pointer]
		wLanguage [uint16]
		lpData [pointer]
		cbData [uint32]
		return: [int32]
	] (kernel32) "UpdateResourceW"]

	EndUpdateResource: make routine! compose [[
		hUpdate [pointer]
		fDiscard [uint8]
		return: [int32]
	] (kernel32) "EndUpdateResourceW"]

	GetLastError: make routine! compose [[
		return: [uint32]
	] (kernel32) "GetLastError"]

	write to file! output read to file! exe
	h: BeginUpdateResource unicodify output 0
	if zero? h [
		print ["failed to open exe"]
		halt
	]
	if zero? UpdateResource h 10
		unicodify "EmbEddEdREbol"
		0 payload-data length? payload-data [
		print ["failed to update resource"]
		halt
	]
	if zero? EndUpdateResource h 0 [
		print ["failed to write change back to the file due to " GetLastError]
		halt
	]
][
	magic: ".EmbEddEdREbol"
	call reform ["objcopy -R" magic exe]
	call rejoin ["objcopy --add-section " magic "=" tmp " " exe " " output]
	delete to file! tmp
]
