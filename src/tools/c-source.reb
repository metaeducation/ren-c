REBOL [
	Title: "C Source Processing"
	Rights: {
		Copyright 2015 Brett Handley
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
	Purpose: {Process Rebol C source, extracting function data, etc.}
]

if error? set/any 'script-error try [

	do %r2r3-future.r
	do %lib/env.reb

	env/log: get in env 'logfn

	script-needs [
		%apropos.reb
		%rebol-c-source.reb
	]

	assert [value? 'apropos]
	assert [value? 'rebol-c-source]

	repo-path: clean-path %../../

	data-path: repo-path/make/(%data/)

	make-dir data-path

	apropos rebol-c-source [
		src-folder: repo-path/(%src/)
		log: :logfn
		scan
	]

	save/header data-path/file-analysis.reb rebol-c-source/cached/files context [
		title: {File analysis}
		date: now
		comment: {This file is generated during the build process.}
	]

	apropos rebol-c-source [
		src-natives: list/natives
		natives-text: generate/natives.r src-natives
		write repo-path/src/boot/tmp-natives.reb natives-text
	]

] [

	?? script-error

	quit/return 1
]
