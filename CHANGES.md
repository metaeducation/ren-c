# Rebol 3 (Ren/C branch) Changes

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/) and is auto-generated using `./scripts/changes-file/make-changes-file.reb`

Alternate ChangeLogs:

* [ChangeLog + pre-built binaries](https://forum.rebol.info/t/rebol3-ren-c-branch-change-logs/54/10000)
* [Complete Github commits](https://github.com/metaeducation/ren-c/commits/master)

Alternatively using Git:

    git log

## [Unreleased]
### Added
- ``` Reinstated %user.reb start-up script (ala R2 %user.r) (#495) ```  *@draegtun* |  [84b5214](https://github.com/metaeducation/ren-c/commit/84b5214) [wiki](https://github.com/r3n/reboldocs/wiki/User-and-Console)
- ``` Add /ONLY refinement to SET, permit single value to block sets ```  *@hostilefork* |  [c1d6173](https://github.com/metaeducation/ren-c/commit/c1d6173)
- ``` Add just DETAB-FILE from pull request, with note, no line deletions ```  *@hostilefork* |  [25a40c6](https://github.com/metaeducation/ren-c/commit/25a40c6)
- ``` Add an encapper for Windows PE ```  *@zsx* |  [8ef2488](https://github.com/metaeducation/ren-c/commit/8ef2488)
- ``` Added CONSOLE! object & skinning ```  *@draegtun* |  [922658f](https://github.com/metaeducation/ren-c/commit/922658f) [00def28](https://github.com/metaeducation/ren-c/commit/00def28)  [2955730](https://github.com/metaeducation/ren-c/commit/2955730) [wiki](https://github.com/r3n/reboldocs/wiki/User-and-Console)
- ``` Introduce source checking of Rebol files. ```  *@codebybrett* |  [46cf8e7](https://github.com/metaeducation/ren-c/commit/46cf8e7)
- ``` voiding SELECT*, PICK*, TAKE* w/convenience wraps SELECT, PICK, TAKE ```  *@hostilefork* |  [33b8f02](https://github.com/metaeducation/ren-c/commit/33b8f02) [3f3aed1](https://github.com/metaeducation/ren-c/commit/3f3aed1)  [eaa5b28](https://github.com/metaeducation/ren-c/commit/eaa5b28)  [84da205](https://github.com/metaeducation/ren-c/commit/84da205) [trello](https://trello.com/c/pWMjGYl7/)
- ``` Customizable userspace ECHO based on HIJACK ```  *@hostilefork* |  [de46d25](https://github.com/metaeducation/ren-c/commit/de46d25)
- ``` Added WITH_FFI to makefile and Win64 release build ```  *@zsx* |  [177dfdc](https://github.com/metaeducation/ren-c/commit/177dfdc) [50430f1](https://github.com/metaeducation/ren-c/commit/50430f1)
- ``` Add SHA256 hashing native to cryptography module ```  *@hostilefork* |  [8b6f1bf](https://github.com/metaeducation/ren-c/commit/8b6f1bf)
- ``` Add experimental DO-ALL native ```  *@hostilefork* |  [afaa179](https://github.com/metaeducation/ren-c/commit/afaa179)
- ``` Add RECYCLE/VERBOSE to Debug build ```  *@hostilefork* |  [38ce4ee](https://github.com/metaeducation/ren-c/commit/38ce4ee)
- ``` Add return specification to LAST so it can return void/functions ```  *@hostilefork* |  [32d3c4c](https://github.com/metaeducation/ren-c/commit/32d3c4c)
- ``` /ASTRAL refinement for TO-STRING ```  *@hostilefork* |  [5786074](https://github.com/metaeducation/ren-c/commit/5786074) [trello](https://trello.com/c/Md1pxsvs/)
- ``` LOOP-WHILE, LOOP-UNTIL, and UNTIL-2 ```  *@hostilefork* |  [0970784](https://github.com/metaeducation/ren-c/commit/0970784) [trello](https://trello.com/c/988j1mjS/)
- ``` Add /deep to mkdir ```  *@zsx* |  [95841c2](https://github.com/metaeducation/ren-c/commit/95841c2)
- ``` Add a function to convert windows path to Rebol path ```  *@zsx* |  [8c94d2f](https://github.com/metaeducation/ren-c/commit/8c94d2f)
- ``` ANY-TYPE! synonym for ANY-VALUE ```  *@hostilefork* |  [6caf195](https://github.com/metaeducation/ren-c/commit/6caf195) [61abd10](https://github.com/metaeducation/ren-c/commit/61abd10) [trello](https://trello.com/c/1jTJXB0d/)
- ``` Add LOGIC! to legal arguments to ASSERT ```  *@hostilefork* |  [4e109a2](https://github.com/metaeducation/ren-c/commit/4e109a2)
- ``` New ELSE & THEN enfix functions ```  *@hostilefork* |  [c1c5945](https://github.com/metaeducation/ren-c/commit/c1c5945) [f3a5bf8](https://github.com/metaeducation/ren-c/commit/f3a5bf8)  [3b3699d](https://github.com/metaeducation/ren-c/commit/3b3699d) [trello](https://trello.com/c/NPivtSdd/)
```rebol

if 1 = 1 [run-this] else [run-that]

```
- ``` <in>, <with>, <static> / <has> function specs ```  *@hostilefork* |  [0e0ae9a](https://github.com/metaeducation/ren-c/commit/0e0ae9a) [47aef8a](https://github.com/metaeducation/ren-c/commit/47aef8a)  [9fb973c](https://github.com/metaeducation/ren-c/commit/9fb973c)  [0dff611](https://github.com/metaeducation/ren-c/commit/0dff611) [trello](https://trello.com/c/IyxPieNa/)
- ``` Custom ENFIX operators.  Killed OP! ```  *@hostilefork* |  [f0304ed](https://github.com/metaeducation/ren-c/commit/f0304ed) [cfae703](https://github.com/metaeducation/ren-c/commit/cfae703) [trello](https://trello.com/c/OvNE3GPM/)
```rebol

>> x: enfix func [a b] [a * b]

>> 2 x 3
== 6

```
- ``` MAYBE, MAYBE? and ENSURE provide convenient inline type checking ```  *@hostilefork* |  [0a28ea5](https://github.com/metaeducation/ren-c/commit/0a28ea5) [de3f9a1](https://github.com/metaeducation/ren-c/commit/de3f9a1)  [19983a9](https://github.com/metaeducation/ren-c/commit/19983a9)  [051f2a9](https://github.com/metaeducation/ren-c/commit/051f2a9)  [4e16471](https://github.com/metaeducation/ren-c/commit/4e16471)  [d5b5b9e](https://github.com/metaeducation/ren-c/commit/d5b5b9e)  [554dce6](https://github.com/metaeducation/ren-c/commit/554dce6) [trello](https://trello.com/c/LR9PzUS3/125)
- ``` Add /WHERE feature to FAIL ```  *@hostilefork* |  [603c1d4](https://github.com/metaeducation/ren-c/commit/603c1d4)
- ``` Add AS and ALIASES? natives ```  *@hostilefork* |  [296c3f5](https://github.com/metaeducation/ren-c/commit/296c3f5)
- ``` Initial experimental HIJACK implementation ```  *@hostilefork* |  [fc7c536](https://github.com/metaeducation/ren-c/commit/fc7c536) [trello](https://trello.com/c/eaumDXoG/)
- ``` Add ANY? and ALL? via CHAIN, XXX? descriptions ```  *@hostilefork* |  [fa984cb](https://github.com/metaeducation/ren-c/commit/fa984cb)
- ``` First draft of ADAPT and CHAIN, function reorganization ```  *@hostilefork* |  [c5ab577](https://github.com/metaeducation/ren-c/commit/c5ab577) [trello](https://trello.com/c/lz9DgFAO/)
```rebol

>> add-one: func [x] [x + 1]

>> mp-ad-ad: chain [:multiply | :add-one | :add-one]

>> mp-ad-ad 10 20
== 202

```
- ``` Add SEMIQUOTE, rename EVALUATED? to SEMIQUOTED? ```  *@hostilefork* |  [2c022f8](https://github.com/metaeducation/ren-c/commit/2c022f8) [trello](https://trello.com/c/rOvj3fHc/)
- ``` Add || userspace "expression barrier that runs one expr" ```  *@hostilefork* |  [112da01](https://github.com/metaeducation/ren-c/commit/112da01)
- ``` IF?, UNLESS?, WHILE?, SWITCH?, etc. ```  *@hostilefork* |  [1daba3f](https://github.com/metaeducation/ren-c/commit/1daba3f) [trello](https://trello.com/c/yy2ezRXi/)
```rebol

>> if? 1 < 2 [print "Branch Taken"]
Branch Taken
== true

```
- ``` Add in a `void` function ```  *@hostilefork* |  [ccac327](https://github.com/metaeducation/ren-c/commit/ccac327)
- ``` Add PUNCTUATOR? and SET-PUNCTUATOR ```  *@hostilefork* |  [37cd465](https://github.com/metaeducation/ren-c/commit/37cd465)
- ``` |> and <| Manipulators ```  *@hostilefork* |  [309b221](https://github.com/metaeducation/ren-c/commit/309b221) [trello](https://trello.com/c/fpA2ryJw/)
```rebol

>> 10 + 20 <| print "Hello" 100 + 200
Hello
== 30

```
- ``` Add RUNNING? and PENDING? tests for FRAME! ```  *@hostilefork* |  [cb9c221](https://github.com/metaeducation/ren-c/commit/cb9c221)
- ``` Make lone underscore literal "BLANK!/NONE!" ```  *@hostilefork* |  [213b804](https://github.com/metaeducation/ren-c/commit/213b804) [trello](https://trello.com/c/vJTaG3w5/)
- ``` Add VARIADIC?, make EVAL prototype "honest" ```  *@hostilefork* |  [28ec47b](https://github.com/metaeducation/ren-c/commit/28ec47b)
- ``` *experimental* VARARGS! - highly flexible variadics ```  *@hostilefork* |  [efbaef9](https://github.com/metaeducation/ren-c/commit/efbaef9) [2aac4ed](https://github.com/metaeducation/ren-c/commit/2aac4ed) [trello](https://trello.com/c/Y17CEywN/)
```rebol

>> foo: func [x [integer! <variadic>]] [
        print ["foo takes" take x "and then" take x]
    ]

>> foo 1 2
foo takes 1 and then 2

>> foo 1 + 2 3 + 4
foo takes 3 and then 7

```
- ``` Add `ok?` as synonym for `not error?` ```  *@hostilefork* |  [460ce81](https://github.com/metaeducation/ren-c/commit/460ce81)
- ``` New PROC and PROCEDURE function generators ```  *@hostilefork* |  [240783a](https://github.com/metaeducation/ren-c/commit/240783a) [trello](https://trello.com/c/gJsjAhKQ/)
```rebol

 >> foo: procedure [] [leave print "This won't print"]
 >> foo
 ; No result

 >> foo: proc [y] [y + 20]
 >> foo 10
 ; No result

```
- ``` Add FIND map key ```  *@giuliolunati* |  [b002615](https://github.com/metaeducation/ren-c/commit/b002615) [trello](https://trello.com/c/DWm8wJA9/)
```rebol

>> m: map [a 1 b 2 c 3]
== make map! [
    a 1
    b 2
    c 3
]

>> find m 'b            
== true

>> find m 'd 
== false

```
- ``` Add REMOVE/MAP map key ```  *@giuliolunati* |  [fc6a4ee](https://github.com/metaeducation/ren-c/commit/fc6a4ee)
- ``` Expression barriers: BAR! and LIT-BAR! ```  *@hostilefork* |  [c84a17c](https://github.com/metaeducation/ren-c/commit/c84a17c) [trello](https://trello.com/c/7RbcHZX3/)
```rebol

>> reduce [1 + 1 | 2 + 2 | 4 + 4]
== [2 | 4 | 8]

```
- ``` Add decode-key-value-text function. ```  *@codebybrett* |  [354f8fc](https://github.com/metaeducation/ren-c/commit/354f8fc)
- ``` Add CHECK-SET routine per suggestion from @earl ```  *@hostilefork* |  [3c665c3](https://github.com/metaeducation/ren-c/commit/3c665c3)
- ``` GROUP! as new default term for PAREN! ```  *@hostilefork* |  [c78d5a2](https://github.com/metaeducation/ren-c/commit/c78d5a2) [trello](https://trello.com/c/ANlT44nH/)
- ``` Add COLLECT-WITH mezzanine to specify "keeper" ```  *@hostilefork* |  [93d2677](https://github.com/metaeducation/ren-c/commit/93d2677) [trello](https://trello.com/c/FWFFMz68/)
- ``` FOR-SKIP synonym for FORSKIP and FOR-NEXT / FOR-BACK for FORALL ```  *@hostilefork* |  [e8419e6](https://github.com/metaeducation/ren-c/commit/e8419e6) [trello](https://trello.com/c/StCADPIB/)
- ``` Breakpoints + Interactive Debugging ```  *@hostilefork* |  [6711dc2](https://github.com/metaeducation/ren-c/commit/6711dc2)
- ``` Added SOMETHING? & NOTHING? ```  *@hostilefork* |  [fab1090](https://github.com/metaeducation/ren-c/commit/fab1090) [123be17](https://github.com/metaeducation/ren-c/commit/123be17) [trello](https://trello.com/c/QBZGM8GY/)
- ``` Add SET? native, remove x: () disablement from legacy ```  *@hostilefork* |  [e8e30e9](https://github.com/metaeducation/ren-c/commit/e8e30e9)
- ``` <local> tag added to FUNC ```  *@hostilefork* |  [c77b6b4](https://github.com/metaeducation/ren-c/commit/c77b6b4) [trello](https://trello.com/c/WKGaad6F/)
- ``` SPELLING-OF as native function ```  *@hostilefork* |  [9888f21](https://github.com/metaeducation/ren-c/commit/9888f21) [trello](https://trello.com/c/4Ky7vRCb/)
- ``` Add RELAX and OPTIONAL natives, OPT shorthand ```  *@hostilefork* |  [6eb91b0](https://github.com/metaeducation/ren-c/commit/6eb91b0)
- ``` Add CONTINUE/WITH, modify BREAK/WITH, cleanup ```  *@hostilefork* |  [bdd51eb](https://github.com/metaeducation/ren-c/commit/bdd51eb)
- ``` Add FAIL native for raising an error or error-spec ```  *@hostilefork* |  [df1a02a](https://github.com/metaeducation/ren-c/commit/df1a02a) [trello](https://trello.com/c/tnRPtrx0/)
```rebol

fail "Simple error"

```
- ``` New prefix AND? OR? XOR? NOT? functions ```  *@hostilefork* |  [839a3f8](https://github.com/metaeducation/ren-c/commit/839a3f8)
- ``` Added FOR-EACH / EVERY.  Deprecating FOREACH ```  *@hostilefork* |  [89e23d3](https://github.com/metaeducation/ren-c/commit/89e23d3) [trello](https://trello.com/c/cxvHGNha/)
- ``` ANY-LIST! typeset (and ANY-ARRAY! experiment) ```  *@hostilefork* |  [1ffa861](https://github.com/metaeducation/ren-c/commit/1ffa861) [trello](https://trello.com/c/lCSdxtux/)
- ``` TRAP, a proposed replacement for TRY. TRY/EXCEPT => TRAP/WITH ```  *@hostilefork* |  [58007df](https://github.com/metaeducation/ren-c/commit/58007df) [f5c003b](https://github.com/metaeducation/ren-c/commit/f5c003b) [trello](https://trello.com/c/IbnfBaLI)
- ``` Add new pseudo-type "TRASH!" ```  *@hostilefork* |  [5f22bc3](https://github.com/metaeducation/ren-c/commit/5f22bc3)
- ``` Add UTF-16LE/BE codec for strings ```  *@zsx* |  [d417aa2](https://github.com/metaeducation/ren-c/commit/d417aa2)
- ``` Add an encapper for Windows and Linux ```  *@zsx* |  [1286ac5](https://github.com/metaeducation/ren-c/commit/1286ac5)
- ``` FEAT: Windows Serial Implementation and extension of serial functionality ```  *@kealist* |  [3a04abf](https://github.com/metaeducation/ren-c/commit/3a04abf) [adeaa40](https://github.com/metaeducation/ren-c/commit/adeaa40)
- ``` Add submodule libffi and link to static libffi ```  *@zsx* |  [7985d07](https://github.com/metaeducation/ren-c/commit/7985d07)
- ``` Permit SET-WORD! as the argument to COPY and SET in PARSE (CC #2023) ```  *@hostilefork* |  [66b87c9](https://github.com/metaeducation/ren-c/commit/66b87c9) [#CC-2023](https://github.com/rebol/rebol-issues/issues/2023)
```rebol

foo: function [s] [
    parse s [
        {My name is } copy name: to end
    ]
    name
]

```
- ``` Added support for ESC-O escape sequences in addition to ESC-[ ```  *Kevin Harris* |  [75f7320](https://github.com/metaeducation/ren-c/commit/75f7320)
- ``` -added ADLER32 checksum -enhanced RSA for /padding refinement ```  *richard* |  [2b4c5f7](https://github.com/metaeducation/ren-c/commit/2b4c5f7)
- ``` add SET-ENV, tweaks to host api for environment string handling ```  *@hostilefork* |  [b9426a5](https://github.com/metaeducation/ren-c/commit/b9426a5)
### Changed
- ``` Move BROWSE into userspace, built upon CALL ```  *@hostilefork* |  [44c34ae](https://github.com/metaeducation/ren-c/commit/44c34ae)
- ``` Make SET have /PAD semantics by default, add SET/SOME refinement ```  *@hostilefork* |  [58d6d5b](https://github.com/metaeducation/ren-c/commit/58d6d5b)
```rebol

>> a: 10 b: 20
>> set/some [a b] [99]
>> a
== 99
>> b
== 20

```
- ``` Command-line option improvements (#508) ```  *@draegtun* |  [fbe5237](https://github.com/metaeducation/ren-c/commit/fbe5237)
- ``` Host-start --verbose changes (#507) ```  *@draegtun* |  [50e015f](https://github.com/metaeducation/ren-c/commit/50e015f)
- ``` Make WHAT-DIR return copy of system variable ```  *@hostilefork* |  [16aff4c](https://github.com/metaeducation/ren-c/commit/16aff4c)
- ``` Allow SET/PAD to be used with BLANK on BLOCK! targets ```  *@hostilefork* |  [b86b5eb](https://github.com/metaeducation/ren-c/commit/b86b5eb)
- ``` Unify semantics of GET-PATH + PICK*, and SET-PATH! + POKE ```  *@hostilefork* |  [10b3f63](https://github.com/metaeducation/ren-c/commit/10b3f63)
- ``` Simplify legacy GET and SET for contexts ```  *@hostilefork* |  [a17a144](https://github.com/metaeducation/ren-c/commit/a17a144)
- ``` Plain GET returns BLANK! for not set variables, GET-VALUE will error ```  *@hostilefork* |  [b1df8c0](https://github.com/metaeducation/ren-c/commit/b1df8c0)
- ``` Make GET-ENV return blank instead of void when variable not present ```  *@hostilefork* |  [e0ff829](https://github.com/metaeducation/ren-c/commit/e0ff829)
- ``` Update MAKE DATE! and MAKE TIME! for BLOCK! input ```  *@hostilefork* |  [d9114c8](https://github.com/metaeducation/ren-c/commit/d9114c8)
- ``` Make APPEND on port act as WRITE/APPEND in slightly less hacky way ```  *@hostilefork* |  [62d137e](https://github.com/metaeducation/ren-c/commit/62d137e)
- ``` LENGTH-OF, HEAD-OF, TAIL-OF as core names...aliased to shorter ```  *@hostilefork* |  [29ae001](https://github.com/metaeducation/ren-c/commit/29ae001) [trello](https://trello.com/c/4OT7qvdu/)
- ``` Check for end of file newline for all text formats. (#471) ```  *@codebybrett* |  [523a890](https://github.com/metaeducation/ren-c/commit/523a890)
- ``` Bring back DUMP changes, move DUMP out of base ```  *@hostilefork* |  [080b9c6](https://github.com/metaeducation/ren-c/commit/080b9c6)
- ``` Allow hard-quoting of BAR!, yet still disallow QUOTE | ```  *@hostilefork* |  [b24f1ce](https://github.com/metaeducation/ren-c/commit/b24f1ce)
- ``` Disallow PICK on MAP! ```  *@hostilefork* |  [9232b3b](https://github.com/metaeducation/ren-c/commit/9232b3b)
- ``` HELP patches to permit FOR-EACH [key val] lib [help :key] ```  *@hostilefork* |  [92cbbcf](https://github.com/metaeducation/ren-c/commit/92cbbcf)
- ``` Simplifying rewrite of REWORD, TO and THRU of BLANK! are no-ops ```  *@hostilefork* |  [ef25751](https://github.com/metaeducation/ren-c/commit/ef25751)
- ``` Check for END when using a DO rule in PARSE ```  *@hostilefork* |  [1e9621e](https://github.com/metaeducation/ren-c/commit/1e9621e)
- ``` Turn BMP, JPG, GIF & PNG codec into an extension ```  *@zsx* |  [4a89744](https://github.com/metaeducation/ren-c/commit/4a89744) [97ca06b](https://github.com/metaeducation/ren-c/commit/97ca06b)  [4638e58](https://github.com/metaeducation/ren-c/commit/4638e58)  [36d9a74](https://github.com/metaeducation/ren-c/commit/36d9a74)
- ``` Support extensions as dynamically linked libraries ```  *@zsx* |  [ef9975e](https://github.com/metaeducation/ren-c/commit/ef9975e)
- ``` Test @GrahamChiu's s3 upload for travis.yml ```  *@hostilefork* |  [4ab24cd](https://github.com/metaeducation/ren-c/commit/4ab24cd)
- ``` Put REPL and host startup code into lib, not user ```  *@hostilefork* |  [489ca6a](https://github.com/metaeducation/ren-c/commit/489ca6a)
- ``` Simpler/Better "Facade" (e.g. can use TIGHTEN on any function) ```  *@hostilefork* |  [185168f](https://github.com/metaeducation/ren-c/commit/185168f)
- ``` Crude draft of ADD and SUBTRACT on BINARY! ```  *@hostilefork* |  [02de856](https://github.com/metaeducation/ren-c/commit/02de856)
- ``` Use LODEPNG-based encoder for PNG, not %u-png.c version ```  *@hostilefork* |  [f97dfca](https://github.com/metaeducation/ren-c/commit/f97dfca)
- ``` Codecs as FUNCTION!s, can be C natives or custom usermode code ```  *@hostilefork* |  [f18a65f](https://github.com/metaeducation/ren-c/commit/f18a65f) [trello](https://trello.com/c/Q3w0PzK3/)
- ``` Make seeks in PARSE via GET-WORD! reset `begin` between rules ```  *@hostilefork* |  [c7b68ea](https://github.com/metaeducation/ren-c/commit/c7b68ea)
- ``` Split REJOIN into JOIN-ALL and UNSPACED cases ```  *@hostilefork* |  [8494e7a](https://github.com/metaeducation/ren-c/commit/8494e7a)
- ``` LOCK code which is not in modules, run from the command line ```  *@hostilefork* |  [5fda0a7](https://github.com/metaeducation/ren-c/commit/5fda0a7)
- ``` Automatically add backtraces in error generating functions ```  *@hostilefork* |  [de0829c](https://github.com/metaeducation/ren-c/commit/de0829c)
- ``` If arguments aren't evaluating, allow BAR! ```  *@hostilefork* |  [ce433ee](https://github.com/metaeducation/ren-c/commit/ce433ee)
- ``` Make PIPE()-based CALL work asynchronously without PIPE2() ```  *@hostilefork* |  [401a96d](https://github.com/metaeducation/ren-c/commit/401a96d)
- ``` Implement the REPL's I/O as Rebol code ```  *@hostilefork* |  [defc222](https://github.com/metaeducation/ren-c/commit/defc222)
- ``` The only thing you can TO convert a blank into is a BLANK!, while TO BLANK! of any input is a blank. ```  *@hostilefork* |  [4e261b4](https://github.com/metaeducation/ren-c/commit/4e261b4)
- ``` Make SWITCH/DEFAULT handle non-block cases ```  *@hostilefork* |  [26eb8ac](https://github.com/metaeducation/ren-c/commit/26eb8ac) [trello](https://trello.com/c/9ChhSWC4/)
- ``` Make crypt a module ```  *@zsx* |  [fa21de3](https://github.com/metaeducation/ren-c/commit/fa21de3)
- ``` Allow extensions to define their own errors ```  *@zsx* |  [c9fa8c0](https://github.com/metaeducation/ren-c/commit/c9fa8c0)
- ``` Reworked extension mechanism ```  *@zsx* |  [7a8c964](https://github.com/metaeducation/ren-c/commit/7a8c964)
- ``` REDUCE leaves BAR! in blocks, SET and GET tolerate BAR! ```  *@hostilefork* |  [d4bfbaf](https://github.com/metaeducation/ren-c/commit/d4bfbaf)
- ``` In-place Get of variables in contexts return relative values ```  *@hostilefork* |  [d1fd3ff](https://github.com/metaeducation/ren-c/commit/d1fd3ff)
- ``` Initial Implementation of Immutable Source ```  *@hostilefork* |  [da11022](https://github.com/metaeducation/ren-c/commit/da11022) [trello](https://trello.com/c/DXs6gJNr/)
- ``` Disallow `<opt>` on refinement arguments ```  *@hostilefork* |  [22fcd9c](https://github.com/metaeducation/ren-c/commit/22fcd9c)
- ``` New loop return result policy, eliminate BREAK/RETURN ```  *@hostilefork* |  [40db488](https://github.com/metaeducation/ren-c/commit/40db488) [trello](https://trello.com/c/uPiz2jLL/)
- ``` Make /LINES and /STRING work on non-FILE! ports (e.g. URL!) ```  *@hostilefork* |  [9b5e025](https://github.com/metaeducation/ren-c/commit/9b5e025)
- ``` Enhance the MATH dialect ```  *@zsx* |  [d5a216f](https://github.com/metaeducation/ren-c/commit/d5a216f)
- ``` ANY and ALL chain "opt-outs" (return void)...add ANY? and ALL? ```  *@hostilefork* |  [cc6d287](https://github.com/metaeducation/ren-c/commit/cc6d287) [trello](https://trello.com/c/wyXCkx67/)
- ``` Simplify GC disablement, allow GC active during boot ```  *@hostilefork* |  [061ca23](https://github.com/metaeducation/ren-c/commit/061ca23)
- ``` Mark UNTIL reserved for future use ```  *@hostilefork* |  [42e20da](https://github.com/metaeducation/ren-c/commit/42e20da)
- ``` JOIN => JOIN-OF, reserve JOIN, act like R3-Alpha REPEND ```  *@hostilefork* |  [a8c0c19](https://github.com/metaeducation/ren-c/commit/a8c0c19)
- ``` Make return value of CLOSE optional ```  *@zsx* |  [0b483bc](https://github.com/metaeducation/ren-c/commit/0b483bc)
- ``` Make ECHO return void, revert fixture line ending change ```  *@hostilefork* |  [fd9e9e6](https://github.com/metaeducation/ren-c/commit/fd9e9e6)
- ``` Allow FOR-EACH to enumerate all the FUNCTION!s ```  *@hostilefork* |  [dbad6c0](https://github.com/metaeducation/ren-c/commit/dbad6c0)
- ``` Bulletproof QUOTE in PARSE against END mark on input ```  *@hostilefork* |  [5738a5f](https://github.com/metaeducation/ren-c/commit/5738a5f)
- ``` No literal blocks in condition of IF/UNLESS/EITHER/CASE ```  *@hostilefork* |  [587d27a](https://github.com/metaeducation/ren-c/commit/587d27a)
- ``` Switch complex module call to APPLY ```  *@hostilefork* |  [5c13b7b](https://github.com/metaeducation/ren-c/commit/5c13b7b)
- ``` Support AS for ANY-WORD! types ```  *@hostilefork* |  [e99e286](https://github.com/metaeducation/ren-c/commit/e99e286)
- ``` Replace HAS-TYPE? specializations with TYPECHECKER ```  *@hostilefork* |  [f7d1d6d](https://github.com/metaeducation/ren-c/commit/f7d1d6d)
- ``` Prevent failed SET in PARSE from overwriting with BLANK! ```  *@hostilefork* |  [6ea2d48](https://github.com/metaeducation/ren-c/commit/6ea2d48) [trello](https://trello.com/c/o1ykZaXS/)
- ``` checked RETURN: types in function specs, notes in HELP ```  *@hostilefork* |  [abd1b89](https://github.com/metaeducation/ren-c/commit/abd1b89)
- ``` Hacky hash to allow e.g. unique [[1 2] [1 2]] ```  *@hostilefork* |  [e71566f](https://github.com/metaeducation/ren-c/commit/e71566f)
- ``` Move `func [[catch] x][...]` support to <r3-legacy> ```  *@hostilefork* |  [24fbc6e](https://github.com/metaeducation/ren-c/commit/24fbc6e)
- ``` Don't evaluate non-blocks passed to PRINT ```  *@hostilefork* |  [d6f2194](https://github.com/metaeducation/ren-c/commit/d6f2194)
- ``` Unify MAKE and construction syntax, MAKE/TO=>native ```  *@hostilefork* |  [29fff21](https://github.com/metaeducation/ren-c/commit/29fff21)
- ``` REDESCRIBE routine for relabeling functions/args ```  *@hostilefork* |  [45df9cd](https://github.com/metaeducation/ren-c/commit/45df9cd)
- ``` Allow GET-PATH! processing to return voids ```  *@hostilefork* |  [22518a5](https://github.com/metaeducation/ren-c/commit/22518a5)
- ``` Support SEMIQUOTED? testing on variadic parameters ```  *@hostilefork* |  [ed8f42c](https://github.com/metaeducation/ren-c/commit/ed8f42c)
- ``` Allow COPY on BLANK!, FIND and SELECT on FRAME! ```  *@hostilefork* |  [b8a9939](https://github.com/metaeducation/ren-c/commit/b8a9939)
- ``` Allow SPECIALIZE/APPLY of definitional returns ```  *@hostilefork* |  [c1b9127](https://github.com/metaeducation/ren-c/commit/c1b9127)
- ``` Lock functions, legacy function-bodies-mutable switch ```  *@hostilefork* |  [399ef60](https://github.com/metaeducation/ren-c/commit/399ef60)
- ``` not TRUE/NONE, not WORD/NONE but TRUE/FALSE refinements ```  *@hostilefork* |  [4e608f7](https://github.com/metaeducation/ren-c/commit/4e608f7) [trello](https://trello.com/c/H9I0261r/)
- ``` Make FIRST, SECOND, etc. use specialization of PICK ```  *@hostilefork* |  [237b28a](https://github.com/metaeducation/ren-c/commit/237b28a)
- ``` Enhance legacy return-none-instead-of-void feature ```  *@hostilefork* |  [4a14b42](https://github.com/metaeducation/ren-c/commit/4a14b42)
- ``` Make APPLY and SPECIALIZE accept words/paths ```  *@hostilefork* |  [e9bbe4f](https://github.com/metaeducation/ren-c/commit/e9bbe4f)
- ``` Variadic Quoting Infix Lambdas (a.k.a. "short function") ```  *@hostilefork* |  [634c463](https://github.com/metaeducation/ren-c/commit/634c463)
- ``` UNSET => VOID name change ```  *@hostilefork* |  [08fc7e5](https://github.com/metaeducation/ren-c/commit/08fc7e5) [trello](https://trello.com/c/rmsTJueg/)
- ``` make path access fail if not set and not GET-PATH! ```  *@hostilefork* |  [d51d9f6](https://github.com/metaeducation/ren-c/commit/d51d9f6)
- ``` Re-introduce legality of `x: ()` assignments ```  *@hostilefork* |  [50fd51d](https://github.com/metaeducation/ren-c/commit/50fd51d)
- ``` Mold fixes for MAP! ```  *@hostilefork* |  [133200b](https://github.com/metaeducation/ren-c/commit/133200b)
- ``` Move variadic DO to r3-legacy, allow 0-arity DO ```  *@hostilefork* |  [3aa675b](https://github.com/metaeducation/ren-c/commit/3aa675b)
- ``` Move TITLE-OF to user mode code ```  *@hostilefork* |  [c6171b3](https://github.com/metaeducation/ren-c/commit/c6171b3)
- ``` Implement SPEC-OF and BODY-OF for specializations ```  *@hostilefork* |  [08dd85d](https://github.com/metaeducation/ren-c/commit/08dd85d)
- ``` OneFunction: Unify functions under FUNCTION! ```  *@hostilefork* |  [c61daa4](https://github.com/metaeducation/ren-c/commit/c61daa4)
- ``` CASE special handling of expression barriers ```  *@hostilefork* |  [dd18504](https://github.com/metaeducation/ren-c/commit/dd18504)
- ``` Improve error NEAR reporting ```  *@hostilefork* |  [a793a6a](https://github.com/metaeducation/ren-c/commit/a793a6a)
- ``` Allow BAR! to mean "TAKE to END" ```  *@hostilefork* |  [ff8baf9](https://github.com/metaeducation/ren-c/commit/ff8baf9)
- ``` Move REDUCE and COMPOSE to their own file ```  *@hostilefork* |  [a20995a](https://github.com/metaeducation/ren-c/commit/a20995a)
- ``` Legacy switch: `no-reduce-nested-print` ```  *@hostilefork* |  [82ccc96](https://github.com/metaeducation/ren-c/commit/82ccc96)
- ``` Get-path for maps distinguishes UNSET from NONE ```  *@giuliolunati* |  [c7eac45](https://github.com/metaeducation/ren-c/commit/c7eac45)
- ``` Change PICK action for maps ```  *@giuliolunati* |  [70f0f35](https://github.com/metaeducation/ren-c/commit/70f0f35)
- ``` Make case-insensitive hash value for chars ```  *@giuliolunati* |  [b0fbffa](https://github.com/metaeducation/ren-c/commit/b0fbffa)
- ``` Recycle 'zombie' keys in maps ```  *@giuliolunati* |  [dc6cbf4](https://github.com/metaeducation/ren-c/commit/dc6cbf4)
- ``` Case-sensitive maps ```  *@giuliolunati* |  [543faca](https://github.com/metaeducation/ren-c/commit/543faca)
- ``` Unicode keys in maps ```  *@giuliolunati* |  [7de3ea6](https://github.com/metaeducation/ren-c/commit/7de3ea6)
- ``` Maps: remove ad hoc code for small maps ```  *@giuliolunati* |  [9e8b685](https://github.com/metaeducation/ren-c/commit/9e8b685)
- ``` Make `GET path` equivalent to `:path` ```  *@giuliolunati* |  [4294371](https://github.com/metaeducation/ren-c/commit/4294371)
- ``` Upgrade decode-lines. ```  *@codebybrett* |  [9e965f5](https://github.com/metaeducation/ren-c/commit/9e965f5)
- ``` Improve UTF-8 decoding error messages ```  *@hostilefork* |  [7920880](https://github.com/metaeducation/ren-c/commit/7920880)
- ``` Make ASSERT return UNSET! instead of TRUE ```  *@hostilefork* |  [938491c](https://github.com/metaeducation/ren-c/commit/938491c)
- ``` Distinguish opt-any-value! from any-value! ```  *@hostilefork* |  [791c7cd](https://github.com/metaeducation/ren-c/commit/791c7cd)
- ``` HALT as Ctrl-C instead of BREAKPOINT ```  *@hostilefork* |  [9dbc985](https://github.com/metaeducation/ren-c/commit/9dbc985)
- ``` SET/ANY and GET/ANY => SET/OPT and GET/OPT ```  *@hostilefork* |  [7d95daa](https://github.com/metaeducation/ren-c/commit/7d95daa)
- ``` Drop SET? as "ANY-VALUE?" operation pending review ```  *@hostilefork* |  [b890ae7](https://github.com/metaeducation/ren-c/commit/b890ae7)
- ``` Dividing money! amount again returns money!, preserving precision ```  *@earl* |  [d216e30](https://github.com/metaeducation/ren-c/commit/d216e30)
- ``` Reword fix, integrate reword tests from @johnk ```  *@hostilefork* |  [f07ec26](https://github.com/metaeducation/ren-c/commit/f07ec26)
- ``` LOOP accepts logic/none, infinite or no loop ```  *@hostilefork* |  [41736da](https://github.com/metaeducation/ren-c/commit/41736da) [trello](https://trello.com/c/3V57JW68/)
- ``` Adjust DECODE-URL to give back NONE hosts ```  *@hostilefork* |  [4658148](https://github.com/metaeducation/ren-c/commit/4658148)
- ``` Convert canon NONE, UNSET, etc. to globals ```  *@hostilefork* |  [62d2a5a](https://github.com/metaeducation/ren-c/commit/62d2a5a)
- ``` Use NOOP so empty loop looks more intentional ```  *@hostilefork* |  [c43dd18](https://github.com/metaeducation/ren-c/commit/c43dd18)
- ``` EXIT/FROM any stack level, natives ```  *@hostilefork* |  [5e9ec3e](https://github.com/metaeducation/ren-c/commit/5e9ec3e) [trello](https://trello.com/c/noiletwM/)
- ``` Programmatic GDB breakpoint support ```  *@hostilefork* |  [2294dce](https://github.com/metaeducation/ren-c/commit/2294dce)
- ``` Tidy new-line implementation, no bitwise XOR ```  *@hostilefork* |  [9cad6b8](https://github.com/metaeducation/ren-c/commit/9cad6b8)
- ``` Boolean review changes to uni/string flags ```  *@hostilefork* |  [a48bb4a](https://github.com/metaeducation/ren-c/commit/a48bb4a)
- ``` POKE on BITSET! only pokes LOGIC! values ```  *@hostilefork* |  [1c8e2ea](https://github.com/metaeducation/ren-c/commit/1c8e2ea) [trello](https://trello.com/c/RAvLaReG/)
- ``` Sync LOCK, PROTECT meanings for series/typesets ```  *@hostilefork* |  [bdbd45c](https://github.com/metaeducation/ren-c/commit/bdbd45c)
- ``` make-module* functionality => userspace MODULE ```  *@hostilefork* |  [17b2704](https://github.com/metaeducation/ren-c/commit/17b2704)
- ``` ANY-OBJECT! => ANY-CONTEXT! ```  *@hostilefork* |  [7305847](https://github.com/metaeducation/ren-c/commit/7305847)
- ``` Update DO to experimental PARAM/REFINE model ```  *@hostilefork* |  [78173cc](https://github.com/metaeducation/ren-c/commit/78173cc)
- ``` Disallow CONTINUE/WITH from an UNTIL ```  *@hostilefork* |  [e89502b](https://github.com/metaeducation/ren-c/commit/e89502b)
- ``` Permit URL! to be converted to TUPLE! ```  *@hostilefork* |  [ff7199f](https://github.com/metaeducation/ren-c/commit/ff7199f)
- ``` OPTIONAL => OPT, RELAX => TO-VALUE ```  *@hostilefork* |  [8680934](https://github.com/metaeducation/ren-c/commit/8680934)
- ``` SET-WORD! in func spec are "true locals", permit RETURN: ```  *@hostilefork* |  [227419a](https://github.com/metaeducation/ren-c/commit/227419a)
- ``` Definitional Returns. Solved. ```  *@hostilefork* |  [fd5f4d6](https://github.com/metaeducation/ren-c/commit/fd5f4d6) [trello](https://trello.com/c/4Kg3DZ2H/)
- ``` Default IF, WHILE, EVERY to unsets, x: () legal ```  *@hostilefork* |  [778da31](https://github.com/metaeducation/ren-c/commit/778da31)
- ``` Dividing money amounts returns decimal ```  *@hostilefork* |  [a3f249c](https://github.com/metaeducation/ren-c/commit/a3f249c)
- ``` TO-INTEGER/UNSIGNED and conversion fixes ```  *@hostilefork* |  [e9c4fcf](https://github.com/metaeducation/ren-c/commit/e9c4fcf) [trello](https://trello.com/c/I6Y3NWlR/)
- ``` Move FUNCT to be in <r3-legacy> mode only ```  *@hostilefork* |  [bb51e94](https://github.com/metaeducation/ren-c/commit/bb51e94)
- ``` SWITCH uses EQUAL?+STRICT-EQUAL?, more... ```  *@hostilefork* |  [46c1d1f](https://github.com/metaeducation/ren-c/commit/46c1d1f)
- ``` Move /WORD off of TYPE-OF and onto TYPE? ```  *@hostilefork* |  [f1f516d](https://github.com/metaeducation/ren-c/commit/f1f516d)
- ``` AND OR XOR are now conditional operators. New bitwise AND* OR+ XOR+ ```  *@hostilefork* |  [f1e4a20](https://github.com/metaeducation/ren-c/commit/f1e4a20) [#CC-1879](https://github.com/rebol/rebol-issues/issues/1879) [trello](https://trello.com/c/bUnWYu4w/)
- ``` CHANGE-DIR to URL, relative paths for DO of URL ```  *@hostilefork* |  [1a4b541](https://github.com/metaeducation/ren-c/commit/1a4b541)
- ``` Make TAIL? spec support all types EMPTY? does ```  *@hostilefork* |  [eab90ce](https://github.com/metaeducation/ren-c/commit/eab90ce)
- ``` Expand possible /NAME types for THROW ```  *@hostilefork* |  [c94cc95](https://github.com/metaeducation/ren-c/commit/c94cc95) [trello](https://trello.com/c/m94GOELw/)
- ``` QUIT via THROW, CATCH [] vs CATCH/ANY (CC#2247) ```  *@hostilefork* |  [b85f178](https://github.com/metaeducation/ren-c/commit/b85f178) [#CC-2247](https://github.com/rebol/rebol-issues/issues/2247)
- ``` Implement <transparent> attribute behavior for functions ```  *@hostilefork* |  [afe7762](https://github.com/metaeducation/ren-c/commit/afe7762)
- ``` New CASE with switch for legacy behavior (CC#2245) ```  *@hostilefork* |  [cc08fea](https://github.com/metaeducation/ren-c/commit/cc08fea) [#CC-2245](https://github.com/rebol/rebol-issues/issues/2245)
- ``` Handle THROWN() during MAKE ERROR! (CC #2244) ```  *@hostilefork* |  [6bb1346](https://github.com/metaeducation/ren-c/commit/6bb1346) [#CC-2244](https://github.com/rebol/rebol-issues/issues/2244)
- ``` Gabriele Santilli's MATH and FACTORIAL (CC#2120) ```  *@hostilefork* |  [110b9fe](https://github.com/metaeducation/ren-c/commit/110b9fe) [#CC-2120](https://github.com/rebol/rebol-issues/issues/2120)
- ``` EXIT acts as QUIT if not in function, LEGACY flags ```  *@hostilefork* |  [6b017f8](https://github.com/metaeducation/ren-c/commit/6b017f8)
- ``` [catch] function spec block exemption ```  *@hostilefork* |  [242c1e2](https://github.com/metaeducation/ren-c/commit/242c1e2)
- ``` CC#2242 fix of RETURN/THROW in SWITCH ```  *@hostilefork* |  [34bb816](https://github.com/metaeducation/ren-c/commit/34bb816) [#CC-2242](https://github.com/rebol/rebol-issues/issues/2242)
- ``` Enbase fixes for input zero length strings ```  *@hostilefork* |  [85013fe](https://github.com/metaeducation/ren-c/commit/85013fe)
- ``` Error handling overhaul, includes CC#1743 ```  *@hostilefork* |  [9b21568](https://github.com/metaeducation/ren-c/commit/9b21568) [#CC-1743](https://github.com/rebol/rebol-issues/issues/1743)
- ``` Revert "Remove /NOW refinement from QUIT (CC#1743)" ```  *@hostilefork* |  [37c723e](https://github.com/metaeducation/ren-c/commit/37c723e) [#CC-1743](https://github.com/rebol/rebol-issues/issues/1743)
- ``` Interim workaround for CC#2221 ```  *@hostilefork* |  [5267b00](https://github.com/metaeducation/ren-c/commit/5267b00) [#CC-2221](https://github.com/rebol/rebol-issues/issues/2221)
- ``` Switch version number to 2.102.0, for the time being ```  *@earl* |  [89a75c7](https://github.com/metaeducation/ren-c/commit/89a75c7)
- ``` Accept file! to CALL ```  *@zsx* |  [bc6c9b6](https://github.com/metaeducation/ren-c/commit/bc6c9b6)
- ``` Take binary! for I/O redirection in CALL ```  *@zsx* |  [e750611](https://github.com/metaeducation/ren-c/commit/e750611)
- ``` Make /output in CALL take string! instead of word! ```  *@zsx* |  [cd435b5](https://github.com/metaeducation/ren-c/commit/cd435b5)
- ``` Implement a R2-like non-blocking CALL ```  *@zsx* |  [b489d85](https://github.com/metaeducation/ren-c/commit/b489d85)
- ``` CALL with IO redirection ```  *@zsx* |  [1dd682e](https://github.com/metaeducation/ren-c/commit/1dd682e)
- ``` Do not use the system qsort ```  *@zsx* |  [02e7cea](https://github.com/metaeducation/ren-c/commit/02e7cea)
- ``` make library! works ```  *@zsx* |  [dcf71dc](https://github.com/metaeducation/ren-c/commit/dcf71dc)
- ``` TO LOGIC! treats all non-none non-false values as true (CC #2055) ```  *@hostilefork* |  [5e5e1a5](https://github.com/metaeducation/ren-c/commit/5e5e1a5) [#CC-2055](https://github.com/rebol/rebol-issues/issues/2055)
- ``` IF, EITHER, and UNLESS accept all values in then/else slots (CC #2063) ```  *@hostilefork* |  [89c9af5](https://github.com/metaeducation/ren-c/commit/89c9af5) [#CC-2063](https://github.com/rebol/rebol-issues/issues/2063)
- ``` Use R3's rounding implementation instead of round(3). ```  *Marc Simpson* |  [8eed3cd](https://github.com/metaeducation/ren-c/commit/8eed3cd)
- ``` Round decimal division of tuple elements. ```  *Marc Simpson* |  [2fd94f2](https://github.com/metaeducation/ren-c/commit/2fd94f2)
- ``` Make CLOSURE a FUNCT for closures (#2002) ```  *@BrianHawley* |  [b339b0f](https://github.com/metaeducation/ren-c/commit/b339b0f)
- ``` Replace FUNCT with FUNCTION ```  *@BrianHawley* |  [4c6de3b](https://github.com/metaeducation/ren-c/commit/4c6de3b)
- ``` Make FUNCTION a FUNCT alias (#1773) ```  *@BrianHawley* |  [3e6ecf7](https://github.com/metaeducation/ren-c/commit/3e6ecf7)
- ``` cc-1748: Guard protected blocks from /INTO target of REDUCE, COMPOSE ```  *@hostilefork* |  [21fb3c2](https://github.com/metaeducation/ren-c/commit/21fb3c2) [#CC-1748](https://github.com/rebol/rebol-issues/issues/1748)
- ``` Free intermediate buffers used by do-codec (CC #2068) ```  *@hostilefork* |  [e1d8d05](https://github.com/metaeducation/ren-c/commit/e1d8d05) [#CC-2068](https://github.com/rebol/rebol-issues/issues/2068)
- ``` Copied DELETE-DIR from Rebol 2 (cc#1545) ```  *Tamas Herman* |  [bdd7523](https://github.com/metaeducation/ren-c/commit/bdd7523) [#CC-1545](https://github.com/rebol/rebol-issues/issues/1545)
- ``` Implement UDP protocol ```  *@zsx* |  [f929c97](https://github.com/metaeducation/ren-c/commit/f929c97)
- ``` Incorporate /ONLY option for suppressing conditional block evaluation. ```  *@hostilefork* |  [895c893](https://github.com/metaeducation/ren-c/commit/895c893)
- ``` Fix CC-2068 CC-1748 ```  *@carls* |  [9cd51ab](https://github.com/metaeducation/ren-c/commit/9cd51ab) [ac9176a](https://github.com/metaeducation/ren-c/commit/ac9176a)  [be2bd43](https://github.com/metaeducation/ren-c/commit/be2bd43)  [82011e2](https://github.com/metaeducation/ren-c/commit/82011e2) [#CC-1748](https://github.com/rebol/rebol-issues/issues/1748)  [#CC-2068](https://github.com/rebol/rebol-issues/issues/2068)
- ``` Support async read from clipboard ```  *@zsx* |  [038555b](https://github.com/metaeducation/ren-c/commit/038555b)
- ``` String & binary targets for /INTO in REDUCE+COMPOSE (CC #2081) ```  *@hostilefork* |  [4f17ba6](https://github.com/metaeducation/ren-c/commit/4f17ba6) [#CC-2081](https://github.com/rebol/rebol-issues/issues/2081)
- ``` Make word lists of frames bare as suggested Prevent word duplication when appending to an object Fix CC#1979 Optimize the code to not use search Fix 'self handling described in CC#2076 Fix Collect_Object to not overwrite memory BUF_WORDS does not own. Without this fix, Collect_Object was causing crashes in 64-bit R3 Amend Init_Frame_Word to set OPTS_UNWORD option to the word added to the frame word list as suggested. ```  *@ladislav* |  [5e58496](https://github.com/metaeducation/ren-c/commit/5e58496) [#CC-1979](https://github.com/rebol/rebol-issues/issues/1979)  [#CC-2076](https://github.com/rebol/rebol-issues/issues/2076)
- ``` Prevent word duplication when appending to an object Fix CC#1979 Optimize the code to not use search Fix 'self handling described in CC#2076 Rebased on Collect_Obj fix to not cause crashes in 64-bit R3 ```  *@ladislav* |  [4aa7772](https://github.com/metaeducation/ren-c/commit/4aa7772) [#CC-1979](https://github.com/rebol/rebol-issues/issues/1979)  [#CC-2076](https://github.com/rebol/rebol-issues/issues/2076)
- ``` Use Bentley & McIlroy's qsort for sorting strings and blocks Currently, R3 uses platform-specific code for sorting. This may increase the effort necessary to port the interpreter to new platforms. ```  *@ladislav* |  [dd11362](https://github.com/metaeducation/ren-c/commit/dd11362)
- ``` IF, EITHER, and UNLESS accept all values in then/else slots (CC #2063) ```  *@hostilefork* |  [3b16661](https://github.com/metaeducation/ren-c/commit/3b16661) [#CC-2063](https://github.com/rebol/rebol-issues/issues/2063)
- ``` Permit SET-WORD! as the argument to COPY and SET in PARSE (CC #2023) ```  *@hostilefork* |  [8db79a9](https://github.com/metaeducation/ren-c/commit/8db79a9) [#CC-2023](https://github.com/rebol/rebol-issues/issues/2023)
- ``` TO LOGIC! treats all non-none non-false values as true (CC #2055) ```  *@hostilefork* |  [e73dd35](https://github.com/metaeducation/ren-c/commit/e73dd35) [#CC-2055](https://github.com/rebol/rebol-issues/issues/2055)
- ``` cc-1748: Guard protected blocks from /INTO target of REDUCE, COMPOSE ```  *@hostilefork* |  [3efec9c](https://github.com/metaeducation/ren-c/commit/3efec9c) [#CC-1748](https://github.com/rebol/rebol-issues/issues/1748)
- ``` Free intermediate buffers used by do-codec (CC #2068) ```  *@hostilefork* |  [7ae3e7a](https://github.com/metaeducation/ren-c/commit/7ae3e7a) [#CC-2068](https://github.com/rebol/rebol-issues/issues/2068)
- ``` Prevent word duplication when appending to an object Fix CC#1979 Optimize the code to not use search ```  *@ladislav* |  [7372289](https://github.com/metaeducation/ren-c/commit/7372289) [#CC-1979](https://github.com/rebol/rebol-issues/issues/1979)
- ``` Correct multiple inheritance, CC#1863 Deep clone child values Rebind child values Optimize Merge_Frames to use rebinding table Add a new REBIND_TABLE mode to Rebind_Block ```  *@ladislav* |  [27a4fd2](https://github.com/metaeducation/ren-c/commit/27a4fd2) [#CC-1863](https://github.com/rebol/rebol-issues/issues/1863)
- ``` Deep copy functions when cloning objects. Fixes CC#2050 No need to save the block in COPY_DEEP_VALUES since the function does not call Recycle ```  *@ladislav* |  [7f691b0](https://github.com/metaeducation/ren-c/commit/7f691b0) [#CC-2050](https://github.com/rebol/rebol-issues/issues/2050)
- ``` Copied DELETE-DIR from Rebol 2 (cc#1545) ```  *Tamas Herman* |  [24fe03b](https://github.com/metaeducation/ren-c/commit/24fe03b) [#CC-1545](https://github.com/rebol/rebol-issues/issues/1545)
- ``` Improve and speed up object cloning using rebinding Fixes CC#2045 Adjust Rebind_Block to be more compatible with Bind_Block Define rebind modes Adjust Clone_Function to use rebinding ```  *@ladislav* |  [8f82fad](https://github.com/metaeducation/ren-c/commit/8f82fad) [#CC-2045](https://github.com/rebol/rebol-issues/issues/2045)
- ``` HTTP: remove auto-decoding of UTF-8 content ```  *@earl* |  [b68ddee](https://github.com/metaeducation/ren-c/commit/b68ddee)
- ``` Simplify value? change (pull 121, cc 1914) by using existing function. ```  *@carls* |  [264bb4e](https://github.com/metaeducation/ren-c/commit/264bb4e)
- ``` Let the VAUE? function yield #[false] for out-of-scope function variables, which is what the documentation describes. Corrects bug#1914 ```  *@ladislav* |  [1b259d0](https://github.com/metaeducation/ren-c/commit/1b259d0) [#CC-1914](https://github.com/rebol/rebol-issues/issues/1914)
- ``` Use David M. Gay's dtoa for molding decimals ```  *@ladislav* |  [f4ce48e](https://github.com/metaeducation/ren-c/commit/f4ce48e)
- ``` Allow NEW-LINE and NEW-LINE? to accept PAREN! series ```  *@hostilefork* |  [631e698](https://github.com/metaeducation/ren-c/commit/631e698)
- ``` Allow INDEX? of NONE, returning NONE ```  *@earl* |  [370d942](https://github.com/metaeducation/ren-c/commit/370d942)
- ``` Support common Ctrl-D usage as Delete. ```  *Delyan Angelov* |  [b13af70](https://github.com/metaeducation/ren-c/commit/b13af70)
- ``` Support Home and End keys on linux. ```  *Delyan Angelov* |  [0eec0cd](https://github.com/metaeducation/ren-c/commit/0eec0cd)
- ``` REQUEST-FILE/multi returning multiple files is missing the last / in the filenames ```  *@BrianHawley* |  [71e80bd](https://github.com/metaeducation/ren-c/commit/71e80bd)
### Deprecated
- ``` Deprecate CLOSURE => <durable>, PROC, PROCEDURE ```  *@hostilefork* |  [7643bf4](https://github.com/metaeducation/ren-c/commit/7643bf4) [trello](https://trello.com/c/HcRtAnE4/)
### Fixed
- ``` Fixes size? modified? ```  *Graham Chiu* |  [4c29aae](https://github.com/metaeducation/ren-c/commit/4c29aae)
- ``` Fix the home directory on Windows ```  *@zsx* |  [e263431](https://github.com/metaeducation/ren-c/commit/e263431)
- ``` Fix Ctrl-C handling in Windows console IO ```  *@hostilefork* |  [9aaa539](https://github.com/metaeducation/ren-c/commit/9aaa539)
- ``` Fix DNS reverse lookup when given a string ```  *@hostilefork* |  [96121b7](https://github.com/metaeducation/ren-c/commit/96121b7)
- ``` Fix encapped executable name on non-Windows ```  *@zsx* |  [17f6a7e](https://github.com/metaeducation/ren-c/commit/17f6a7e)
- ``` Fix HTTPS false alarm error ```  *@hostilefork* |  [a65de09](https://github.com/metaeducation/ren-c/commit/a65de09)
- ``` Fix some definitional return and definitional leave bugs ```  *@hostilefork* |  [c4e7d04](https://github.com/metaeducation/ren-c/commit/c4e7d04)
- ``` Fix an undefined behavior regarding to union access ```  *@zsx* |  [0abf2b6](https://github.com/metaeducation/ren-c/commit/0abf2b6)
- ``` Fix omitted result in legacy GET behavior ```  *@hostilefork* |  [c5c58cd](https://github.com/metaeducation/ren-c/commit/c5c58cd)
- ``` Fix () vs. (void) argument for make prep ```  *@hostilefork* |  [9754fdb](https://github.com/metaeducation/ren-c/commit/9754fdb)
- ``` Fix error reporting in scripts run from command line ```  *@hostilefork* |  [8813896](https://github.com/metaeducation/ren-c/commit/8813896)
- ``` Fix SET-PATH! in PARSE ```  *@hostilefork* |  [89f2202](https://github.com/metaeducation/ren-c/commit/89f2202)
- ``` Fixes so Ren-C can be used as R3-MAKE ```  *@hostilefork* |  [5a006d1](https://github.com/metaeducation/ren-c/commit/5a006d1)
- ``` Fix RETURN in CHAIN, remove EXIT=QUIT, leaked series on QUIT ```  *@hostilefork* |  [f745cf9](https://github.com/metaeducation/ren-c/commit/f745cf9)
- ``` Fix backward unicode encoding flag ```  *@hostilefork* |  [2f7b0b5](https://github.com/metaeducation/ren-c/commit/2f7b0b5)
- ``` Fix HELP on OBJECT! and PORT! values ```  *Christopher Ross-Gill* |  [b6b9a91](https://github.com/metaeducation/ren-c/commit/b6b9a91)
- ``` Fix system/catalog/* and make their initialization more clear ```  *@hostilefork* |  [5acc74f](https://github.com/metaeducation/ren-c/commit/5acc74f)
- ``` Fix FIND/LAST refinement when used with BLOCK! ```  *@hostilefork* |  [1df7084](https://github.com/metaeducation/ren-c/commit/1df7084)
- ``` Patch for appending a MAP! to a MAP! (used also by COPY) ```  *@hostilefork* |  [c3b442c](https://github.com/metaeducation/ren-c/commit/c3b442c)
- ``` Fix `<in>` feature of FUNCTION and PROCEDURE, use in %sys-start ```  *@hostilefork* |  [bb4bc77](https://github.com/metaeducation/ren-c/commit/bb4bc77)
- ``` Fix CHANGE on struct! ```  *@zsx* |  [14a5d4d](https://github.com/metaeducation/ren-c/commit/14a5d4d)
- ``` Fix trim object! ```  *@zsx* |  [ab9e27c](https://github.com/metaeducation/ren-c/commit/ab9e27c)
- ``` Fix sequence point issue in ENBASE ```  *@giuliolunati* |  [f45767d](https://github.com/metaeducation/ren-c/commit/f45767d)
- ``` Fix search for /LAST in FIND assert on short strings ```  *@hostilefork* |  [6ee4583](https://github.com/metaeducation/ren-c/commit/6ee4583)
- ``` Fix LIST-DIR return result, %mezz-files.r meddling ```  *@hostilefork* |  [82c0bf6](https://github.com/metaeducation/ren-c/commit/82c0bf6)
- ``` Fixes for BLANK! and empty block in PARSE handling, tests ```  *@hostilefork* |  [b7fac96](https://github.com/metaeducation/ren-c/commit/b7fac96)
- ``` Fix DEFAULT of not set bug, add tests ```  *@hostilefork* |  [49371ea](https://github.com/metaeducation/ren-c/commit/49371ea)
- ``` Fix PARSE? (was returning void instead of TRUE/FALSE) ```  *@hostilefork* |  [dee59da](https://github.com/metaeducation/ren-c/commit/dee59da)
- ``` Fix REMOVE copying of implicit terminator, related cleanups ```  *@hostilefork* |  [bb49492](https://github.com/metaeducation/ren-c/commit/bb49492)
- ``` Fix TUPLE! crash after payload switch, round money ```  *@hostilefork* |  [2c7d58f](https://github.com/metaeducation/ren-c/commit/2c7d58f)
- ``` Fix PRIN error message ```  *@hostilefork* |  [08767f2](https://github.com/metaeducation/ren-c/commit/08767f2)
- ``` Fix two SPEC-OF bugs where param and type not being added to the result correctly. ```  *@codebybrett* |  [31327fd](https://github.com/metaeducation/ren-c/commit/31327fd)
- ``` Fix sequence point problem in `read/lines` ```  *@hostilefork* |  [c72e8d3](https://github.com/metaeducation/ren-c/commit/c72e8d3)
- ``` Fix typeset molding ```  *@hostilefork* |  [87ffb37](https://github.com/metaeducation/ren-c/commit/87ffb37)
- ``` Patch recursive MAP! molding bug ```  *@hostilefork* |  [18b1695](https://github.com/metaeducation/ren-c/commit/18b1695)
- ``` Fix IS function and add /RELAX refinement ```  *@hostilefork* |  [89df6ae](https://github.com/metaeducation/ren-c/commit/89df6ae)
- ``` fix 2138: parse tag in block ```  *@giuliolunati* |  [3b54770](https://github.com/metaeducation/ren-c/commit/3b54770)
- ``` Loose the limit on the number of function arguments ```  *@zsx* |  [5ed7c50](https://github.com/metaeducation/ren-c/commit/5ed7c50)
- ``` Fix molding of typesets ```  *@hostilefork* |  [76741d5](https://github.com/metaeducation/ren-c/commit/76741d5)
- ``` Fix assert/problem with CLOSURE!+THROW ```  *@hostilefork* |  [dcae241](https://github.com/metaeducation/ren-c/commit/dcae241)
- ``` Fix any-object? alias ```  *@hostilefork* |  [f7fcfdc](https://github.com/metaeducation/ren-c/commit/f7fcfdc)
- ``` Fix actor dispatch (read http://, etc.) ```  *@hostilefork* |  [505a7a3](https://github.com/metaeducation/ren-c/commit/505a7a3)
- ``` Fix "make object! none" cases ```  *@hostilefork* |  [9a0bffe](https://github.com/metaeducation/ren-c/commit/9a0bffe)
- ``` Fix offset of default command in dialect parsing ```  *@zsx* |  [a4fff1d](https://github.com/metaeducation/ren-c/commit/a4fff1d)
- ``` Fix mezz function ARRAY ```  *@zsx* |  [19cc56f](https://github.com/metaeducation/ren-c/commit/19cc56f)
- ``` Fix precedence bug in file open permissions ```  *@hostilefork* |  [f22b554](https://github.com/metaeducation/ren-c/commit/f22b554)
- ``` Fix WAIT without timeout ```  *@zsx* |  [e833e9e](https://github.com/metaeducation/ren-c/commit/e833e9e)
- ``` Fix suffix? and rename it suffix-of ```  *@giuliolunati* |  [0962677](https://github.com/metaeducation/ren-c/commit/0962677)
- ``` Fix apparently longstanding FORM OBJECT! bug ```  *@hostilefork* |  [decba66](https://github.com/metaeducation/ren-c/commit/decba66)
- ``` Fix case-sensitivity in string sort, support unicode, #2170 ```  *@hostilefork* |  [894174b](https://github.com/metaeducation/ren-c/commit/894174b)
- ``` Fix THROWN() handling for PAREN! in PATH! (CC#2243) ```  *@hostilefork* |  [b0b4416](https://github.com/metaeducation/ren-c/commit/b0b4416) [#CC-2243](https://github.com/rebol/rebol-issues/issues/2243)
- ``` Fix out-of-context word in Reword function. ```  *Christopher Ross-Gill* |  [5dc4b48](https://github.com/metaeducation/ren-c/commit/5dc4b48)
- ``` Fix halt in 'forever []' and similar (CC#2229) ```  *@hostilefork* |  [bfc2604](https://github.com/metaeducation/ren-c/commit/bfc2604) [#CC-2229](https://github.com/rebol/rebol-issues/issues/2229)
- ``` Fix LAUNCH to properly work with argv-based CALL ```  *@earl* |  [9f6e56e](https://github.com/metaeducation/ren-c/commit/9f6e56e)
- ``` Fix cc-2224 by simply disallowing LENGTH? on ANY-WORD! ```  *@hostilefork* |  [5c2c263](https://github.com/metaeducation/ren-c/commit/5c2c263) [#CC-2224](https://github.com/rebol/rebol-issues/issues/2224)
- ``` PowerPC/Big Endian processor unicode fix ```  *@hostilefork* |  [94b001b](https://github.com/metaeducation/ren-c/commit/94b001b)
- ``` Fix break from remove-each ```  *@zsx* |  [d8ae8e8](https://github.com/metaeducation/ren-c/commit/d8ae8e8)
- ``` Fix foreach with set-word ```  *@zsx* |  [3b386a5](https://github.com/metaeducation/ren-c/commit/3b386a5)
- ``` Fix a divided-by-zero error ```  *@zsx* |  [87cf612](https://github.com/metaeducation/ren-c/commit/87cf612)
- ``` Fix PARSE position capture combined with SET or COPY ```  *@earl* |  [23750aa](https://github.com/metaeducation/ren-c/commit/23750aa)
- ``` Fix vector! for 64-bit systems ```  *@zsx* |  [e0296bc](https://github.com/metaeducation/ren-c/commit/e0296bc)
- ``` Fix MD5 on 64-bit Linux ```  *@cyphre* |  [689c95f](https://github.com/metaeducation/ren-c/commit/689c95f)
- ``` Fix 32-bit type used in MD5 ```  *@cyphre* |  [65d0b18](https://github.com/metaeducation/ren-c/commit/65d0b18)
- ``` fix ticket1457 parse to bitset ```  *@giuliolunati* |  [ecfcde7](https://github.com/metaeducation/ren-c/commit/ecfcde7)
- ``` -fixed zlib bug caused PNG encode operation to crash ```  *@cyphre* |  [a5fea46](https://github.com/metaeducation/ren-c/commit/a5fea46)
- ``` Update fix of CC#851 and CC#1896 ```  *@ladislav* |  [16c6867](https://github.com/metaeducation/ren-c/commit/16c6867) [#CC-851](https://github.com/rebol/rebol-issues/issues/851)  [#CC-1896](https://github.com/rebol/rebol-issues/issues/1896)
- ``` Fix CATCH/quit interaction with TRY (cc#851) ```  *@earl* |  [ee69898](https://github.com/metaeducation/ren-c/commit/ee69898) [#CC-851](https://github.com/rebol/rebol-issues/issues/851)
- ``` Fix CATCH/QUIT, CC#851, CC#1896. , the bug causing 3 test-framework crashes. ```  *@ladislav* |  [7ef62e8](https://github.com/metaeducation/ren-c/commit/7ef62e8) [#CC-851](https://github.com/rebol/rebol-issues/issues/851)  [#CC-1896](https://github.com/rebol/rebol-issues/issues/1896)
- ``` Fix compare crash, bug#1049 ```  *@ladislav* |  [12a1ce8](https://github.com/metaeducation/ren-c/commit/12a1ce8) [#CC-1049](https://github.com/rebol/rebol-issues/issues/1049)
- ``` Correct SHIFT bug#2067 Replace undefined arithmetic Detect overflow of arithmetic shift ```  *@ladislav* |  [89eb6ab](https://github.com/metaeducation/ren-c/commit/89eb6ab) [#CC-2067](https://github.com/rebol/rebol-issues/issues/2067)
- ``` Fix circular block compare crash, CC#1049 ```  *@ladislav* |  [bd83d2b](https://github.com/metaeducation/ren-c/commit/bd83d2b) [#CC-1049](https://github.com/rebol/rebol-issues/issues/1049)
- ``` Correct SHIFT bug#2067 Replace undefined arithmetic Detect overflow of arithmetic shift ```  *@ladislav* |  [88269f4](https://github.com/metaeducation/ren-c/commit/88269f4) [#CC-2067](https://github.com/rebol/rebol-issues/issues/2067)
- ``` Modify INTEGER! multiplication to not use division for overflow detection. Modify INTEGER! addition and subtraction to not use undefined arithmetic. Modify integer comparison to not use undefined arithmetic. Fix bug#2054 Do not use MAX_NEG_INT in t_integer.c (problematic name). Amend a comment referring to reb-config.h ```  *@ladislav* |  [55001ab](https://github.com/metaeducation/ren-c/commit/55001ab) [#CC-2054](https://github.com/rebol/rebol-issues/issues/2054)
- ``` Correct object cloning bug#2049 by cloning closures. ```  *@ladislav* |  [7fef89d](https://github.com/metaeducation/ren-c/commit/7fef89d) [#CC-2049](https://github.com/rebol/rebol-issues/issues/2049)
- ``` Use function args as frame reference of function variables instead of function body. Function body was unsuitable (unsafe) for the purpose since in R3 it is possible to define distinct functions sharing the same body. Corrects bug#2025 ```  *@ladislav* |  [3561c16](https://github.com/metaeducation/ren-c/commit/3561c16) [#CC-2025](https://github.com/rebol/rebol-issues/issues/2025)
- ``` Multiple bug fixes from Carl, Earl & Ladislav ```  *@carls* |  [8baf6ca](https://github.com/metaeducation/ren-c/commit/8baf6ca) [19d1111](https://github.com/metaeducation/ren-c/commit/19d1111)  [2b52014](https://github.com/metaeducation/ren-c/commit/2b52014)  [2b2ee73](https://github.com/metaeducation/ren-c/commit/2b2ee73)  [d82b061](https://github.com/metaeducation/ren-c/commit/d82b061)  [24836e6](https://github.com/metaeducation/ren-c/commit/24836e6)  [d566711](https://github.com/metaeducation/ren-c/commit/d566711)  [0ea6b08](https://github.com/metaeducation/ren-c/commit/0ea6b08)  [32a6be5](https://github.com/metaeducation/ren-c/commit/32a6be5)  [740a8c1](https://github.com/metaeducation/ren-c/commit/740a8c1)  [42653da](https://github.com/metaeducation/ren-c/commit/42653da)  [8a0ffb9](https://github.com/metaeducation/ren-c/commit/8a0ffb9)  [c5c40d2](https://github.com/metaeducation/ren-c/commit/c5c40d2) [#CC-1989](https://github.com/rebol/rebol-issues/issues/1989)  [#CC-1969](https://github.com/rebol/rebol-issues/issues/1969)  [#CC-1957](https://github.com/rebol/rebol-issues/issues/1957)  [#CC-1958](https://github.com/rebol/rebol-issues/issues/1958)  [#CC-1491](https://github.com/rebol/rebol-issues/issues/1491)
- ``` Correct problem with HTTP READ after correcting bug#2025 ```  *@ladislav* |  [65a8017](https://github.com/metaeducation/ren-c/commit/65a8017) [#CC-2025](https://github.com/rebol/rebol-issues/issues/2025)
- ``` Let BIND bind out-of scope function words. Corrects bug#1983. ```  *@ladislav* |  [66876d4](https://github.com/metaeducation/ren-c/commit/66876d4) [#CC-1983](https://github.com/rebol/rebol-issues/issues/1983)
- ``` Let Clone_Function copy the args block of the original, letting the args block identify the function and making it suitable for frame reference. Corrects bug#2044. ```  *@ladislav* |  [9bd75a0](https://github.com/metaeducation/ren-c/commit/9bd75a0) [#CC-2044](https://github.com/rebol/rebol-issues/issues/2044)
- ``` Remove message print when LOAD/NEXT is used. Correct comment typos. Corrects bug#2041 ```  *@ladislav* |  [1092a9d](https://github.com/metaeducation/ren-c/commit/1092a9d) [#CC-2041](https://github.com/rebol/rebol-issues/issues/2041)
- ``` Use function args as frame reference of function variables instead of function body. Function body was unsuitable (unsafe) for the purpose since in R3 it is possible to define distinct functions sharing the same body. Corrects bug#2025 ```  *@ladislav* |  [6f0050a](https://github.com/metaeducation/ren-c/commit/6f0050a) [#CC-2025](https://github.com/rebol/rebol-issues/issues/2025)
- ``` Let the VAUE? function yield #[false] for out-of-scope function variables, which is what the documentation describes. bug#1914 ```  *@ladislav* |  [0fe7266](https://github.com/metaeducation/ren-c/commit/0fe7266) [#CC-1914](https://github.com/rebol/rebol-issues/issues/1914)
- ``` Fix SET object/block block assuming /any (#1763) ```  *@BrianHawley* |  [dac7455](https://github.com/metaeducation/ren-c/commit/dac7455)
- ``` Fix PARSE regression: THRU not matching at the end ```  *@earl* |  [eaf0d94](https://github.com/metaeducation/ren-c/commit/eaf0d94)
- ``` bug#1957 corr, FIRST+ help strings updated ```  *@ladislav* |  [824aab6](https://github.com/metaeducation/ren-c/commit/824aab6) [#CC-1957](https://github.com/rebol/rebol-issues/issues/1957)
- ``` bug#1958, BIND and UNBIND can work on blocks not containing (just) words ```  *@ladislav* |  [4ac3424](https://github.com/metaeducation/ren-c/commit/4ac3424) [#CC-1958](https://github.com/rebol/rebol-issues/issues/1958)
- ``` bug#1844, modification properties ```  *@ladislav* |  [5ff6627](https://github.com/metaeducation/ren-c/commit/5ff6627) [#CC-1844](https://github.com/rebol/rebol-issues/issues/1844)
- ``` bug#1955, updates of malconstruct error and mold help string ```  *@ladislav* |  [276eed3](https://github.com/metaeducation/ren-c/commit/276eed3) [#CC-1955](https://github.com/rebol/rebol-issues/issues/1955)
- ``` bug#1956 ```  *@ladislav* |  [0d68c29](https://github.com/metaeducation/ren-c/commit/0d68c29) [#CC-1956](https://github.com/rebol/rebol-issues/issues/1956)
- ``` bug#1939 ```  *@ladislav* |  [b1e845b](https://github.com/metaeducation/ren-c/commit/b1e845b) [#CC-1939](https://github.com/rebol/rebol-issues/issues/1939)
- ``` Fix crash when reading dns:// without a hostname ```  *@earl* |  [d2dce76](https://github.com/metaeducation/ren-c/commit/d2dce76)
- ``` Fixed #1875 - random/only bug ```  *DanDLee* |  [af37b35](https://github.com/metaeducation/ren-c/commit/af37b35)
- ``` Fix CC#1865: resolve/extend/only crash ```  *@earl* |  [00bee60](https://github.com/metaeducation/ren-c/commit/00bee60) [#CC-1865](https://github.com/rebol/rebol-issues/issues/1865)
### Removed
- ``` Get rid of DELECT ```  *@hostilefork* |  [8769bb9](https://github.com/metaeducation/ren-c/commit/8769bb9)
- ``` Delete unimplemented TASK! stub ```  *@hostilefork* |  [dfcd893](https://github.com/metaeducation/ren-c/commit/dfcd893)
- ``` Eliminate reified UNSET! and datatype ```  *@hostilefork* |  [bbf615d](https://github.com/metaeducation/ren-c/commit/bbf615d)
- ``` Remove use of SPLIT from parser which fails on linux when used with a rule that contains THRU. ```  *@codebybrett* |  [1e98be5](https://github.com/metaeducation/ren-c/commit/1e98be5)
- ``` Remove Markup Codec ```  *@hostilefork* |  [4797130](https://github.com/metaeducation/ren-c/commit/4797130)
- ``` Get rid of RM as unix `remove` ```  *@hostilefork* |  [2a7fcbb](https://github.com/metaeducation/ren-c/commit/2a7fcbb)
- ``` Get rid of UTYPE! stub, datatype=>symbol fix ```  *@hostilefork* |  [a25c359](https://github.com/metaeducation/ren-c/commit/a25c359)
- ``` Get rid of RETURN/redo ```  *@BrianHawley* |  [49b94e5](https://github.com/metaeducation/ren-c/commit/49b94e5) [trello](https://trello.com/c/YMAb89dv/)
- ``` Remove /NOW refinement from QUIT (CC#1743) ```  *@hostilefork* |  [e93a5b4](https://github.com/metaeducation/ren-c/commit/e93a5b4) [#CC-1743](https://github.com/rebol/rebol-issues/issues/1743)
- ``` Get rid of Lit-Word decay (CC#2101, CC#1434) ```  *@hostilefork* |  [f027870](https://github.com/metaeducation/ren-c/commit/f027870) [#CC-2101](https://github.com/rebol/rebol-issues/issues/2101)  [#CC-1434](https://github.com/rebol/rebol-issues/issues/1434) [trello](https://trello.com/c/MVLLDYJW/)
- ``` Remove deprecated 'register' keyword ```  *@hostilefork* |  [8e5b9b1](https://github.com/metaeducation/ren-c/commit/8e5b9b1)
- ``` Get rid of LOAD /next ```  *@BrianHawley* |  [8da64f8](https://github.com/metaeducation/ren-c/commit/8da64f8)
- ``` Get rid of if/else ```  *@BrianHawley* |  [23673df](https://github.com/metaeducation/ren-c/commit/23673df)
- ``` Remove message print when LOAD/NEXT is used. bug#2041 ```  *@ladislav* |  [93b05f7](https://github.com/metaeducation/ren-c/commit/93b05f7) [#CC-2041](https://github.com/rebol/rebol-issues/issues/2041)
## [R3-Alpha] - 12-Dec-2012
### Added
- ``` Initial source release ```  *@carls* |  [08eb7e8](https://github.com/metaeducation/ren-c/commit/08eb7e8)
- ``` R3 license ```  *@carls* |  [8b682e5](https://github.com/metaeducation/ren-c/commit/8b682e5)
- ``` Initial commit ```  *@carls* |  [19d4f96](https://github.com/metaeducation/ren-c/commit/19d4f96)


For Github user names see CREDITS.md
