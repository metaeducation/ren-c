REBOL [
	Title: "R3 Encap code"
]

show-console

print "R3 Encapper (c)2012 Saphirion AG"

encrypt: funct [
	data [string! binary!]
][
	random/seed "REBOL"
	out: copy #{} 
	foreach c reverse enbase/base data 64 [append out (to integer! c) + random 100]
	out
]

either script-file: system/options/script [
	
	print ["Including script:" script-file]
	
	script: include/only script-file
	
	insert script compose [
		system/script/header: (first load/header script-file)
	]
	
	prin "Encapping data..."
	
	key: form now/precise
	enc: encrypt key
	beg: to-binary length? encap-exe
	len-enc: length? enc
	
	header: rejoin [
		enc
		len-enc
		beg
	]
	
	append exe: copy encap-exe rejoin [
		encloak compress mold/all/only script key
		header
	]
	
	print "OK"
	
	out-name: any [
		all [system/options/args to file! system/options/args/1]
		join copy/part script-file find/last script-file suffix? script-file %.exe
	]
	print ["Writing" out-name]
	write out-name exe
	print "Encapping done."
][
	print "USAGE: r3encap.exe script [output_exe]"
]

error? try [ask "** Press enter to quit..."]
quit