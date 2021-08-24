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

%api/librebol.test.reb

%datatypes/action.test.reb
%datatypes/bad-word.test.reb
%datatypes/binary.test.reb
%datatypes/bitset.test.reb
%datatypes/blank.test.reb
%datatypes/block.test.reb
%datatypes/char.test.reb
%datatypes/datatype.test.reb
%datatypes/date.test.reb
%datatypes/decimal.test.reb
%datatypes/dictionary.test.reb
%datatypes/email.test.reb
%datatypes/error.test.reb
%datatypes/event.test.reb
%datatypes/file.test.reb
%datatypes/frame.test.reb
%datatypes/get-block.test.reb
%datatypes/get-group.test.reb
%datatypes/get-path.test.reb
%datatypes/get-word.test.reb
%datatypes/gob.test.reb
%datatypes/hash.test.reb
%datatypes/image.test.reb
%datatypes/integer.test.reb
%datatypes/issue.test.reb
%datatypes/lit-path.test.reb
%datatypes/lit-word.test.reb
%datatypes/logic.test.reb
%datatypes/module.test.reb
%datatypes/money.test.reb
%datatypes/null.test.reb
%datatypes/object.test.reb
%datatypes/pair.test.reb
%datatypes/paren.test.reb
%datatypes/path.test.reb
%datatypes/percent.test.reb
%datatypes/port.test.reb
%datatypes/quoted.test.reb
%datatypes/refinement.test.reb
%datatypes/set-block.test.reb
%datatypes/set-group.test.reb
%datatypes/set-path.test.reb
%datatypes/set-word.test.reb
%datatypes/meta-block.test.reb
%datatypes/meta-group.test.reb
%datatypes/meta-path.test.reb
%datatypes/meta-word.test.reb
%datatypes/string.test.reb
%datatypes/tag.test.reb
%datatypes/time.test.reb
%datatypes/tuple.test.reb
%datatypes/typeset.test.reb
%datatypes/url.test.reb
%datatypes/varargs.test.reb
%datatypes/word.test.reb

%comparison/lesserq.test.reb
%comparison/maximum-of.test.reb
%comparison/equalq.test.reb
%comparison/sameq.test.reb
%comparison/strict-equalq.test.reb
%comparison/strict-not-equalq.test.reb

%context/bind.test.reb
%context/boundq.test.reb
%context/bindq.test.reb
%context/collect-words.test.reb
%context/resolve.test.reb
%context/set.test.reb
%context/unset.test.reb
%context/use.test.reb
%context/valueq.test.reb
%context/virtual-bind.test.reb

%control/all.test.reb
%control/any.test.reb
%control/attempt.test.reb
%control/case.test.reb
%control/catch.test.reb
%control/compose.test.reb
%control/default.test.reb
%control/disarm.test.reb
%control/do.test.reb
%control/either.test.reb
%control/else.test.reb
%control/halt.test.reb
%control/if.test.reb
%control/match.test.reb
%control/reduce.test.reb
%control/reeval.test.reb
%control/switch.test.reb
%control/throw.test.reb
%control/try.test.reb
%control/unless.test.reb
%control/wait.test.reb
%control/quit.test.reb
%convert/as-binary.test.reb
%convert/as-string.test.reb
%convert/enbin.test.reb
%convert/encode.test.reb
%convert/mold.test.reb
%convert/to.test.reb

%define/func.test.reb

%convert/to-hex.test.reb

%file/clean-path.test.reb
%file/existsq.test.reb
%file/make-dir.test.reb
%file/open.test.reb
%file/split-path.test.reb
%file/file-typeq.test.reb

%functions/adapt.test.reb
%functions/augment.test.reb
%functions/chain.test.reb
%functions/does.test.reb
%functions/enclose.test.reb
%functions/enfix.test.reb
%functions/frame.test.reb
%functions/function.test.reb
%functions/hijack.test.reb
%functions/invisible.test.reb
%functions/let.test.reb
%functions/literal.test.reb
%functions/macro.test.reb
%functions/multi.test.reb
%functions/native.test.reb
%functions/oneshot.test.reb
%functions/predicate.test.reb
%functions/redo.test.reb
%functions/reframer.test.reb
%functions/return.test.reb
%functions/specialize.test.reb
%functions/unwind.test.reb

%loops/break.test.reb
%loops/cfor.test.reb
%loops/continue.test.reb
%loops/count-up.test.reb
%loops/cycle.test.reb
%loops/every.test.reb
%loops/for.test.reb
%loops/for-next.test.reb
%loops/iterate.test.reb
%loops/iterate-skip.test.reb
%loops/loop.test.reb
%loops/map.test.reb
%loops/remove-each.test.reb
%loops/repeat.test.reb
%loops/until.test.reb

%math/absolute.test.reb
%math/add.test.reb
%math/and.test.reb
%math/arcsine.test.reb
%math/arctangent.test.reb
%math/complement.test.reb
%math/cosine.test.reb
%math/difference.test.reb
%math/divide.test.reb
%math/evenq.test.reb
%math/exp.test.reb
%math/log-10.test.reb
%math/log-2.test.reb
%math/log-e.test.reb
%math/mod.test.reb
%math/multiply.test.reb
%math/negate.test.reb
%math/negativeq.test.reb
%math/not.test.reb
%math/oddq.test.reb
%math/positiveq.test.reb
%math/power.test.reb
%math/random.test.reb
%math/remainder.test.reb
%math/round.test.reb
%math/shift.test.reb
%math/signq.test.reb
%math/sine.test.reb
%math/square-root.test.reb
%math/subtract.test.reb
%math/tangent.test.reb
%math/zeroq.test.reb

%misc/assert.test.reb
%misc/help.test.reb
%misc/fail.test.reb
%misc/make-file.test.reb
%misc/shell.test.reb
%misc/unpack.test.reb

%network/http.test.reb

%parse/parse3.test.reb
%parse/parse3-collect.test.reb
%parse/parse-across.test.reb
%parse/parse-action.test.reb
%parse/parse-ahead.test.reb
%parse/parse-between.test.reb
%parse/parse-bitset.test.reb
%parse/parse-binary.test.reb
%parse/parse-blank.test.reb
%parse/parse-block.test.reb
%parse/parse-break.test.reb
%parse/parse-change.test.reb
%parse/parse-collect.test.reb
%parse/parse-datatype.test.reb
%parse/parse-elide.test.reb
%parse/parse-further.test.reb
%parse/parse-furthest.test.reb
%parse/parse-gather.test.reb
%parse/parse-get-group.test.reb
%parse/parse-group.test.reb
%parse/parse-insert.test.reb
%parse/parse-integer.test.reb
%parse/parse-into.test.reb
%parse/parse-issue.test.reb
%parse/parse-logic.test.reb
%parse/parse-measure.test.reb
%parse/parse-meta-xxx.test.reb
%parse/parse-not.test.reb
%parse/parse-opt.test.reb
%parse/parse-path.test.reb
%parse/parse-phase.test.reb
%parse/parse-quoted.test.reb
%parse/parse-remove.test.reb
%parse/parse-repeat.test.reb
%parse/parse-return.test.reb
%parse/parse-seek.test.reb
%parse/parse-set-word.test.reb
%parse/parse-some.test.reb
%parse/parse-skip.test.reb
%parse/parse-stop.test.reb
%parse/parse-tag-any.test.reb
%parse/parse-tag-end.test.reb
%parse/parse-tag-here.test.reb
%parse/parse-tag-index.test.reb
%parse/parse-tally.test.reb
%parse/parse-text.test.reb
%parse/parse-the-xxx.test.reb
%parse/parse-thru.test.reb
%parse/parse-to.test.reb
%parse/parse-while.test.reb
%parse/parse-word.test.reb
%parse/uparse2.test.reb
%parse/examples/argtest.parse.test.reb
%parse/examples/breaker.parse.test.reb
%parse/examples/countify.parse.test.reb
%parse/examples/expression.parse.test.reb
%parse/examples/html-parser.parse.test.reb
%parse/examples/maxmatch.parse.test.reb
%parse/examples/nanbnc.parse.test.reb
%parse/examples/reword.parse.test.reb
%parse/examples/split.parse.test.reb
%parse/examples/tracked-word.parse.test.reb
%parse/examples/trim.parse.test.reb
%parse/examples/urldecode.parse.test.reb

%redbol/redbol-apply.test.reb

%reflectors/body-of.test.reb

%scanner/load.test.reb
%scanner/path-tuple.test.reb
%scanner/source-comment.test.reb

%secure/const.test.reb
%secure/protect.test.reb
%secure/unprotect.test.reb

%series/append.test.reb
%series/as.test.reb
%series/at.test.reb
%series/back.test.reb
%series/change.test.reb
%series/charset.test.reb
%series/clear.test.reb
%series/collect.test.reb
%series/copy.test.reb
%series/delimit.test.reb
%series/emptyq.test.reb
%series/exclude.test.reb
%series/extract.test.reb
%series/find.test.reb
%series/free.test.reb
%series/glom.test.reb
%series/indexq.test.reb
%series/insert.test.reb
%series/intersect.test.reb
%series/just.test.reb
%series/join.test.reb
%series/last.test.reb
%series/lengthq.test.reb
%series/next.test.reb
%series/only.test.reb
%series/ordinals.test.reb
%series/pick.test.reb
%series/poke.test.reb
%series/remove.test.reb
%series/reverse.test.reb
%series/replace.test.reb
%series/reword.test.reb
%series/select.test.reb
%series/skip.test.reb
%series/sort.test.reb
%series/split.test.reb
%series/tailq.test.reb
%series/trim.test.reb
%series/union.test.reb
%series/unique.test.reb

%string/compress.test.reb
%string/decode.test.reb
%string/encode.test.reb
%string/decompress.test.reb
%string/dehex.test.reb
%string/transcode.test.reb
%string/utf8.test.reb

%system/system.test.reb
%system/file.test.reb
%system/gc.test.reb


; !!! These tests require the named extensions to be built in.  Whether the
; test is run or not should depend on whether the extension is present.  TBD.

%../extensions/vector/tests/vector.test.reb
%../extensions/process/tests/call.test.reb
%../extensions/dns/tests/dns.test.reb


; SOURCE ANALYSIS: Check to make sure the Rebol files are "lint"-free, and
; enforce any policies (no whitespace at end of line, etc.)

%source/text-lines.test.reb
%source/analysis.test.reb
