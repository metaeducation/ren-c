; Rebol []
; *****************************************************************************
; Title: Rebol core tests
; Copyright:
;     2012 REBOL Technologies
;     2013 Saphirion AG
; Author:
;     Carl Sassenrath, Ladislav Mecir, Andreas Bolka, Brian Hawley, John K
; License:
;     Licensed under the Apache License, Version 2.0 (the "License");
;     you may not use this file except in compliance with the License.
;     You may obtain a copy of the License at
;
;     http://www.apache.org/licenses/LICENSE-2.0
;
; *****************************************************************************

%api/librebol.test.r

%datatypes/action.test.r
%datatypes/binary.test.r
%datatypes/bitset.test.r
%datatypes/blank.test.r
%datatypes/block.test.r
%datatypes/chain.test.r
%datatypes/char.test.r
%datatypes/datatype.test.r
%datatypes/date.test.r
%datatypes/decimal.test.r
%datatypes/dictionary.test.r
%datatypes/email.test.r
%datatypes/error.test.r
%datatypes/fence.test.r
%datatypes/file.test.r
%datatypes/frame.test.r
%datatypes/get-block.test.r
%datatypes/get-group.test.r
%datatypes/get-path.test.r
%datatypes/get-word.test.r
%datatypes/hash.test.r
%datatypes/integer.test.r
%datatypes/lit-path.test.r
%datatypes/lit-word.test.r
%datatypes/logic.test.r
%datatypes/module.test.r
%datatypes/money.test.r
; %datatypes/money-math.test.r  ; not currently working
%datatypes/null.test.r
%datatypes/object.test.r
%datatypes/pair.test.r
%datatypes/paren.test.r
%datatypes/parameter.test.r
%datatypes/path.test.r
%datatypes/percent.test.r
%datatypes/port.test.r
%datatypes/quasi.test.r
%datatypes/quoted.test.r
%datatypes/refinement.test.r
%datatypes/rune.test.r
%datatypes/set-block.test.r
%datatypes/set-group.test.r
%datatypes/set-path.test.r
%datatypes/set-word.test.r
%datatypes/meta.test.r
%datatypes/meta-block.test.r
%datatypes/meta-group.test.r
%datatypes/meta-path.test.r
%datatypes/meta-word.test.r
%datatypes/sigil.test.r
%datatypes/string.test.r
%datatypes/the.test.r
%datatypes/tag.test.r
%datatypes/time.test.r
%datatypes/tuple.test.r
%datatypes/url.test.r
%datatypes/varargs.test.r
%datatypes/word.test.r

%comparison/lesserq.test.r
%comparison/maximum-of.test.r
%comparison/equalq.test.r
%comparison/sameq.test.r
%comparison/strict-equalq.test.r
%comparison/strict-not-equalq.test.r

%context/bind.test.r
%context/boundq.test.r
%context/bindq.test.r
%context/collect-words.test.r
%context/resolve.test.r
%context/set.test.r
%context/unset.test.r
%context/use.test.r
%context/valueq.test.r
%context/virtual-bind.test.r
%context/wrap.test.r

%control/all.test.r
%control/any.test.r
%control/case.test.r
%control/catch.test.r
%control/compose.test.r
%control/default.test.r
%control/destructure.test.r
%control/do.test.r
%control/either.test.r
%control/else.test.r
%control/halt.test.r
%control/if.test.r
%control/match.test.r
%control/reduce.test.r
%control/reeval.test.r
%control/switch.test.r
%control/unless.test.r
%control/wait.test.r
%control/quit.test.r
%control/examples/switch2.control.test.r

%convert/as-binary.test.r
%convert/as-string.test.r
%convert/enbin.test.r
%convert/encode.test.r
%convert/mold.test.r
%convert/to.test.r

%convert/to-hex.test.r

%define/func.test.r

%errors/disarm.test.r
%errors/except.test.r
%errors/rescue.test.r
%errors/require.test.r
%errors/trap.test.r

%examples/circled.test.r
%examples/flow.test.r
%examples/n-queens.test.r
%examples/prime-factors.test.r
%examples/sudoku-solver.test.r

%file/clean-path.test.r
%file/file-port.test.r
%file/existsq.test.r
%file/make-dir.test.r
%file/open.test.r
%file/split-path.test.r
%file/file-typeq.test.r

%functions/adapt.test.r
%functions/augment.test.r
%functions/cascade.test.r
%functions/does.test.r
%functions/enclose.test.r
%functions/frame.test.r
%functions/function.test.r
%functions/generator.test.r
%functions/hijack.test.r
%functions/infix.test.r
%functions/invisible.test.r
%functions/let.test.r
%functions/literal.test.r
%functions/macro.test.r
%functions/multi.test.r
%functions/native.test.r
%functions/oneshot.test.r
%functions/pointfree.test.r
%functions/predicate.test.r
%functions/reframer.test.r
; %functions/reorder.test.r  ; REORDER is broken at the moment
%functions/return.test.r
%functions/shove.test.r
%functions/specialize.test.r
%functions/typechecker.test.r
%functions/unwind.test.r
%functions/yielder.test.r

%loops/attempt.test.r
%loops/break.test.r
%loops/cfor.test.r
%loops/continue.test.r
%loops/count-up.test.r
%loops/cycle.test.r
%loops/every.test.r
%loops/for.test.r
%loops/for-next.test.r
%loops/iterate.test.r
%loops/iterate-skip.test.r
%loops/insist.test.r
%loops/map.test.r
%loops/remove-each.test.r
%loops/repeat.test.r
%loops/while.test.r
%loops/examples/for-both.loops.test.r
%loops/examples/for-parallel.loops.test.r

%math/absolute.test.r
%math/add.test.r
%math/and.test.r
%math/arcsine.test.r
%math/arctangent.test.r
%math/complement.test.r
%math/cosine.test.r
%math/difference.test.r
%math/divide.test.r
%math/evenq.test.r
%math/exp.test.r
%math/log-10.test.r
%math/log-2.test.r
%math/log-e.test.r
%math/math.test.r
%math/mod.test.r
%math/multiply.test.r
%math/negate.test.r
%math/negativeq.test.r
%math/not.test.r
%math/oddq.test.r
%math/positiveq.test.r
%math/power.test.r
%math/random.test.r
%math/remainder.test.r
%math/round.test.r
%math/shift.test.r
%math/signq.test.r
%math/sine.test.r
%math/square-root.test.r
%math/subtract.test.r
%math/tangent.test.r
%math/zeroq.test.r

%network/http.test.r

%parse/parse.test.r
%parse/parse3.test.r
%parse/parse-match.test.r
%parse/parse-thru.test.r
%parse/parse.accept.test.r
%parse/parse.accumulate.test.r
%parse/parse.across.test.r
%parse/parse.action.test.r
%parse/parse.ahead.test.r
%parse/parse.any.test.r
%parse/parse.between.test.r
%parse/parse.bitset.test.r
%parse/parse.binary.test.r
%parse/parse.blank.test.r
%parse/parse.block.test.r
%parse/parse.break.test.r
%parse/parse.change.test.r
%parse/parse.collect.test.r
%parse/parse.cond.test.r
%parse/parse.datatype.test.r
%parse/parse.elide.test.r
%parse/parse.further.test.r
%parse/parse.furthest.test.r
%parse/parse.gather.test.r
%parse/parse.get-group.test.r
%parse/parse.group.test.r
%parse/parse.insert.test.r
%parse/parse.just.test.r
%parse/parse.let.test.r
%parse/parse.literal.test.r
%parse/parse.maybe.test.r
%parse/parse.measure.test.r
%parse/parse.meta-xxx.test.r
%parse/parse.not.test.r
%parse/parse.one.test.r
%parse/parse.optional.test.r
%parse/parse.path.test.r
%parse/parse.phase.test.r
%parse/parse.quasi.test.r
%parse/parse.quoted.test.r
%parse/parse.remove.test.r
%parse/parse.repeat.test.r
%parse/parse.rune.test.r
%parse/parse.seek.test.r
%parse/parse.set-word.test.r
%parse/parse.set-group.test.r
%parse/parse.skip.test.r
%parse/parse.some.test.r
%parse/parse.stop.test.r
%parse/parse.subparse.test.r
%parse/parse.tag-end.test.r
%parse/parse.tag-here.test.r
%parse/parse.tag-index.test.r
%parse/parse.tally.test.r
%parse/parse.text.test.r
%parse/parse.the-xxx.test.r
%parse/parse.thru.test.r
%parse/parse.to.test.r
%parse/parse.validate.test.r
%parse/parse.veto.test.r
%parse/parse.void.test.r
%parse/parse.word.test.r
%parse/examples/argtest.parse.test.r
%parse/examples/breaker.parse.test.r
%parse/examples/countify.parse.test.r
%parse/examples/evaluate.parse.test.r
%parse/examples/expression.parse.test.r
%parse/examples/function3.parse.test.r
%parse/examples/html-parser.parse.test.r
%parse/examples/maxmatch.parse.test.r
%parse/examples/nanbnc.parse.test.r
%parse/examples/red-replace.parse.test.r
%parse/examples/reword.parse.test.r
%parse/examples/split.parse.test.r
%parse/examples/topaz-expression.parse.test.r
%parse/examples/tracked-word.parse.test.r
%parse/examples/trim.parse.test.r
%parse/examples/ugly-parse.parse.test.r
%parse/examples/validate-enclosure.parse.test.r

%reflectors/body-of.test.r

%scanner/load.test.r
%scanner/path-tuple.test.r
%scanner/source-comment.test.r

%secure/const.test.r
%secure/protect.test.r
%secure/unprotect.test.r

%series/append.test.r
%series/as.test.r
%series/at.test.r
%series/back.test.r
%series/change.test.r
%series/charset.test.r
%series/clear.test.r
%series/collect.test.r
%series/copy.test.r
%series/delimit.test.r
%series/emptyq.test.r
%series/exclude.test.r
%series/extract.test.r
%series/envelop.test.r
%series/find.test.r
%series/free.test.r
%series/glom.test.r
%series/indexq.test.r
%series/insert.test.r
%series/intersect.test.r
%series/join.test.r
%series/last.test.r
%series/lengthq.test.r
%series/next.test.r
%series/ordinals.test.r
%series/pick.test.r
%series/poke.test.r
%series/remove.test.r
%series/reverse.test.r
%series/replace.test.r
%series/reword.test.r
%series/select.test.r
%series/skip.test.r
%series/sort.test.r
%series/split.test.r
%series/tailq.test.r
%series/trim.test.r
%series/union.test.r
%series/unique.test.r

%string/codepoint.test.r
%string/compress.test.r
%string/decode.test.r
%string/encode.test.r
%string/decompress.test.r
%string/dehex.test.r
%string/interpolate.test.r
%string/transcode.test.r
%string/utf8.test.r

%system/system.test.r
%system/file.test.r
%system/gc.test.r


; !!! These tests require the named extensions to be built in.  Whether the
; test is run or not should depend on whether the extension is present.  TBD.

%../extensions/process/tests/call.test.r
%../extensions/dns/tests/dns.test.r

%../extensions/crypt/tests/aes.test.r
%../extensions/crypt/tests/adler32.test.r
%../extensions/crypt/tests/crc32.test.r
%../extensions/crypt/tests/dh.test.r
%../extensions/crypt/tests/ecc.test.r
%../extensions/crypt/tests/md5.test.r
%../extensions/crypt/tests/rsa.test.r
%../extensions/crypt/tests/sha1.test.r
%../extensions/crypt/tests/sha256.test.r

%misc/assert.test.r
%misc/pack-old.test.r
%misc/panic.test.r
%misc/help.test.r  ; Do this last, as it has a lot of output


; SOURCE ANALYSIS: Check to make sure the Rebol files are "lint"-free, and
; enforce any policies (no whitespace at end of line, etc.)

%source/text-lines.test.r
%source/analysis.test.r
