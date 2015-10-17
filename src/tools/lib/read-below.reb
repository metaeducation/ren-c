; file: https://raw.githubusercontent.com/codebybrett/reb/master/read-below.reb
; date: 17-Oct-2015/15:24:42+11:00

Rebol [
	Title: "read-below"
	Date: 16-Oct-2015
	File: %read-below.reb
	Purpose: {Reads all files and directories below specified directory.}
	Version: 1.5.0
	Author: "Brett Handley"
	History: [
		1.5.0 [16-Oct-2015 {Added /trace and dirize workaround for old rebols.}]
		1.4.0 [13-Oct-2015 {Use FAIL instead of making error directly.}]
		1.3.1 [12-Nov-2013 {Added read-below-paths} "Brett Handley"]
		1.3.0 [11-May-2013 {Changed to work with REBOL 3 Alpha.} "Brett Handley"]
		1.2.0 [25-Sep-2011 {Changed behaviour of /exclude. Was still including top level of excluded directors, no longer doing that.} "Brett Handley"]
		1.1.0 [13-Mar-2005 {Added /exclude refinement.} "Brett Handley"]
		1.0.0 [17-May-2004 {First version} "Brett Handley"]
	]
	Library: [
		level: 'intermediate
		platform: 'all
		type: 'tool
		domain: 'file-handling
		tested-under: [
			core 2.5.6.31 on [WinNT4] {Basic tests.} "Brett"
		]
		support: none
		license: none
		comment: {
Copyright (C) 2015 Brett Handley All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the author nor the names of its contributors may be used to
endorse or promote products derived from this software without specific
prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  
}

		see-also: none
	]
]

script-needs [
	%file-tests.reb
]

read-below: func [
	{Read all directories below and including a given file path.}
	[catch throw]
	path [file! url!] "Must be a directory (ending in a trailing slash)."
	/exclude exclude-files [file! url! block!] "Directories/files to be excluded from recursion/result."
	/foreach "Evaluates a block for each file or directory found."
	'word [word!] "Word set to each file or directory."
	body [block!] "Block to evaluate for each file or directory."
	/trace {Logs each folder read.}
	/local queue file-list result file do-func *foreach files folder log
] [

	*foreach: get bind 'foreach 'do

	log: if trace [
		func [message] [print mold compose/only message]
	]

	if #"/" <> last path [
		fail "read-below expected path to have trailing slash."
	]

	exclude-files: compose [(any [exclude-files []])]

	; Initialise parameters
	if not foreach [
		word: 'file
		file-list: make block! 10000
		body: [insert tail file-list file]
	]

	; Create process function
	do-func: func compose [[throw] (:word)] body

	; Initialise queue
	queue: read path
	log [folder (path) (new-line/all queue true)]

	; Process queue
	set/any 'result if not empty? queue [
		until [
			file: first queue
			queue: remove queue
			if is-dir? file [file: dirize file]
			if not find exclude-files file [
				do-func file
				if #"/" = last file [
					files: read folder: join path file
					log [folder (folder) (new-line/all files true)]
					*foreach f files [insert queue join file f]
					queue: head queue
				]
			]
			tail? queue
		]
	]

	; Return result.
	if not foreach [result: file-list]
	get/any 'result
]


read-below-paths: funct [
	paths [block!]
][
	collect [
		foreach path paths [
			list: read-below path
			repeat i length list [poke list i join :path list/:i]
			keep list
		]
	]
]
