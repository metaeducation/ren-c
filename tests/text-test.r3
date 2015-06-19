REBOL [Title: "Basic TEXT test"]

do %gfx-pre.r3

find-file: funct [
	dir [file!]
	file [file!]
][
	foreach f read dir [
		fn: dir/:f
		either dir? fn [
			if fn: find-file fn file [
				return fn
			]
		][
			if file = f [
				return to string! fn
			]
		]
	]
	return none
]

find-font: funct [
	"Simple way how to find specific font file on Linux"
	font-name [string! file!]
][
	font-dirs: copy []

	fonts-cfg: to-string read %/etc/fonts/fonts.conf

	parse/all fonts-cfg [
		some [
			thru {<dir>} copy dir to {</dir>} (append font-dirs dirize to-file dir)
		]
	]
	foreach font-dir probe font-dirs [
		if all [
			exists? font-dir
			f: find-file font-dir font-name
		][
			return f
		]
	]
	return none
]

fnt-uni: make system/standard/font [
	size: 28
]

switch system/version/4 [
	3 [ ; Windows
		fnt-uni/name: "Arial Unicode MS"
	]
	4 [ ; Linux
		fnt-uni/name: find-font %FreeSans.ttf		
	]
]

draw-block: to-draw [
	text [
	anti-alias
	font fnt-uni
	size 28
	underline
	bold
	"Unicode text works in HostKit!"
	drop 2
	newline
	size 11
	"You need 'Arial Unicode MS' truetype font for this demo or some other font with wide range of unicode support"
	newline
	drop 1
	underline off	
	bold off
	navy	
	"Arabic - ضطفقحڭڦڞ۞"
	newline
	"Armenian - ՅՌՎՑՓ"
	newline
	"Bengali - তঃঊঋঐকতোতৢ"
	newline
	"Chinese - ㌇㌌㌚㌫㍀㍌㍖"
	newline
	"Czech - ěščřžýáíéňď"
	newline
	"Greek - αβγδεζ"
	newline
	"German - äßÖöü"
	newline
	"Hebrew - סאבגדהוט"
	newline
	"Hiragana - ばぬぢぽみゆあ"
	newline
	"Katakana - ゼヂネポヸダジ"
	newline
	"Panjabi - ੨ਫ਼ਓਔੴ"
	newline
	"Russian - ДφψЗлйжҒ"
	newline
	"Thai - ฑญฆญจบฟ"
	newline
	"Tibetan - གྷཆ༰༯༲༬༣༇༈༊ང྆ཀྵ"
	]
] copy []

print "Generating TEXT graphics.."
img: make image! [420x640 164.200.255]
write %text.png encode 'png draw img draw-block
print "Output has been succesfully written to text.png file."
halt
