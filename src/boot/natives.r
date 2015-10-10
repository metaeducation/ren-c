REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "Native function specs"
	Rights: {
		Copyright 2012 REBOL Technologies
		REBOL is a trademark of REBOL Technologies
	}
	License: {
		Licensed under the Apache License, Version 2.0.
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Note: [
		"Used to generates C enums and tables"
		"Boot bind attributes are SET and not DEEP"
		"Todo: before beta release remove extra/unused refinements"
	]
]

;-- Control Natives - nat_control.c

ajoin: native [
	{Reduces and joins a block of values into a new string.}
	block [block!]
]

also: native [
	{Returns the first value, but also evaluates the second.}
	value1 [any-value!]
	value2 [any-value!]
]

all: native [
	{Shortcut AND. Evaluates and returns at the first FALSE or NONE.}
	block [block!] {Block of expressions}
]

any: native [
	{Shortcut OR. Evaluates and returns the first value that is not FALSE or NONE.}
	block [block!] {Block of expressions}
]

apply: native [
	{Apply a function to a reduced block of arguments.}
	func [any-function!] "Function value to apply"
	block [block!] "Block of args, reduced first (unless /only)"
	/only "Use arg values as-is, do not reduce the block"
]

assert: native [
	"Assert that condition is true, else cause an assertion error."
	conditions [block!]
	/type "Safely check datatypes of variables (words and paths)"
]

attempt: native [
	"Tries to evaluate a block and returns result or NONE on error."
	block [block!]
]

break: native [
	{Breaks out of a loop, while, until, repeat, for-each, etc.}
	/with {Forces the loop function to return a value}
	value [any-value!]
	/return {(deprecated synonym for /WITH)}
	return-value [any-value!]
]

case: native [
	{Evaluates each condition, and when true, evaluates what follows it.}
	block [block!] {Block of cases (conditions followed by values)}
	/all {Evaluate all cases (do not stop at first TRUE? case)}
	/only {Return block values instead of evaluating them.}
]

catch: native [
	{Catches a throw from a block and returns its value.}
	block [block!] {Block to evaluate}
	/name {Catches a named throw}
	name-list [block! word! any-function! object!] {Names to catch (single name if not block)}
	/quit {Special catch for QUIT native}
	/any {Catch all throws except QUIT (can be used with /QUIT)}
	/with {Handle thrown case with code}
	handler [block! any-function!] {If FUNCTION!, spec matches [value name]}
]

comment: native [
	{Ignores the argument value and returns nothing (no evaluations performed).}
	:value [block! any-string! any-scalar!] {Literal value to be ignored.}
]

compose: native [
	{Evaluates a block of expressions, only evaluating parens, and returns a block.}
	value "Block to compose"
	/deep "Compose nested blocks"
	/only {Insert a block as a single value (not the contents of the block)}
	/into {Output results into a series with no intermediate storage}
	out [any-array! any-string! binary!]
]

context: native [
	{Defines a unique object.}
	spec [block!] {Object words and values (modified)}
]

continue: native [
	{Throws control back to top of loop.}
]

;dir?: native [
;	{Returns true if file is a directory.}
;	file [any-string! none!]
;	/any {Allow * or ? wildcards for directory}
;]

;disarm: native [
;	{(Deprecated - not needed) Converts error to an object. Other types not modified.}
;	error [any-value!]
;]

do: native [
	{Evaluates a block of source code (directly or fetched according to type)}
	;source [none! block! paren! string! binary! url! file! tag!]
	; !!! Actually does not handle ERROR! or ANY-FUNCTION! but temporarily
	; accepts them to trigger more informataive errors suggesting FAIL and EVAL
	source [none! block! paren! string! binary! url! file! tag! error! any-function!]
	/args "If value is a script, this will set its system/script/args"
	arg   "Args passed to a script (normally a string)"
	/next "Do next expression only, return it, update block variable"
	var [word!] "Variable updated with new block position"
]

eval: native [
	{(Special) Process received value *inline* as the evaluator loop would.}
	value [any-value!] {BLOCK! passes-thru, FUNCTION! runs, SET-WORD! assigns...}
]

either: native [
	{If TRUE condition return first arg, else second; evaluate blocks by default.}
	condition
	true-branch [any-value!]
	false-branch [any-value!]
	/only "Suppress evaluation of block args."
]

every: native [
	{Returns last TRUE? value if evaluating a block over a series is all TRUE?}
	'word [word! block!] {Word or block of words to set each time (local)}
	data [any-series! any-object! map! none!] {The series to traverse}
	body [block!] {Block to evaluate each time}
]

exit: native [
	{Leave whatever enclosing Rebol state EXIT's block *actually* runs in.}
	/with {Result for enclosing state (default is UNSET!)}
	value [any-value!]
]

fail: native [
	{Interrupts execution by reporting an error (a TRAP can intercept it).}
	reason [error! string! block!] {ERROR! value, message string, or failure spec}
]

find-script: native [
	{Find a script header within a binary string. Returns starting position.}
	script [binary!]
]

for: native [
	{Evaluate a block over a range of values. (See also: REPEAT)}
	'word [word!] "Variable to hold current value"
	start [any-series! any-number!] "Starting value"
	end   [any-series! any-number!] "Ending value"
	bump  [any-number!] "Amount to skip each time"
	body  [block!] "Block to evaluate"
]

forall: native [
	"Evaluates a block for every value in a series."
	'word [word!] {Word that refers to the series, set to each position in series}
	body [block!] "Block to evaluate each time"
]

forever: native [
	{Evaluates a block endlessly.}
	body [block!] {Block to evaluate each time}
]

for-each: native [
	{Evaluates a block for each value(s) in a series.}
	'word [word! block!] {Word or block of words to set each time (local)}
	data [any-series! any-object! map! none!] {The series to traverse}
	body [block!] {Block to evaluate each time}
]

forskip: native [
	"Evaluates a block for periodic values in a series."
	'word [word!] {Word that refers to the series, set to each position in series}
	size [integer! decimal!] "Number of positions to skip each time"
	body [block!] "Block to evaluate each time"
	/local orig result
]

halt: native [
	{Stops evaluation and returns to the input prompt.}
]

if: native [
	{If TRUE condition, return arg; evaluate blocks by default.}
	condition
	true-branch [any-value!]
	/only "Return block arg instead of evaluating it."
]

loop: native [
	{Evaluates a block a specified number of times.}
	count [any-number!] {Number of repetitions}
	block [block!] {Block to evaluate}
]

map-each: native [
	{Evaluates a block for each value(s) in a series and returns them as a block.}
	'word [word! block!] {Word or block of words to set each time (local)}
	data [block! vector!] {The series to traverse}
	body [block!] {Block to evaluate each time}
]

;replace-all: native [
;	"Search and replace multiple values with a series; returns a new series."
;	target [block! string! binary!]
;	values [block!] "A block of [old new] search/replace pairs"
;]

quit: native [
	{Stop evaluating and return control to command shell or calling script.}
	/with {Yield a result (mapped to an integer if given to shell)}
	value [any-value!] {See: http://en.wikipedia.org/wiki/Exit_status}
	/return {(deprecated synonym for /WITH)}
	return-value
]

protect: native [
	"Protect a series or a variable from being modified."
	value [word! any-series! bitset! map! object! module!]
	/deep "Protect all sub-series/objects as well"
	/words  "Process list as words (and path words)"
	/values "Process list of values (implied GET)"
	/hide "Hide variables (avoid binding and lookup)"
]

unprotect: native [
	"Unprotect a series or a variable (it can again be modified)."
	value [word! any-series! bitset! map! object! module!]
	/deep "Protect all sub-series as well"
	/words "Block is a list of words"
	/values "Process list of values (implied GET)"
]

recycle: native [
	{Recycles unused memory.}
	/off {Disable auto-recycling}
	/on {Enable auto-recycling}
	/ballast {Trigger for auto-recycle (memory used)}
	size [integer!]
	/torture {Constant recycle (for internal debugging)}
]

reduce: native [
	{Evaluates expressions and returns multiple results.}
	value
	/no-set {Keep set-words as-is. Do not set them.}
	/only {Only evaluate words and paths, not functions}
	words [block! none!] {Optional words that are not evaluated (keywords)}
	/into {Output results into a series with no intermediate storage}
	out [any-array! any-string! binary!]
]

repeat: native [
	{Evaluates a block a number of times or over a series.}
	'word [word!] {Word to set each time}
	value [any-number! any-series! none!] {Maximum number or series to traverse}
	body [block!] {Block to evaluate each time}
]

remove-each: native [
	{Removes values for each block that returns true; returns removal count.}
	'word [word! block!] {Word or block of words to set each time (local)}
	data [any-series!] {The series to traverse (modified)}
	body [block!] {Block to evaluate (return TRUE to remove)}
]

return: native [
	{Returns a value from a function.}
	value [any-value!]
]

switch: native [
	"Selects a choice and evaluates the block that follows it."
	value "Target value"
	cases [block!] "Block of cases to check"
	/default case "Default case if no others found"
	/all "Evaluate all matches (not just first one)"
	/strict "Use STRICT-EQUAL? when comparing cases instead of EQUAL?"
]

throw: native [
	{Throws control back to a previous catch.}
	value [any-value!] {Value returned from catch}
	/name {Throws to a named catch}
	name-value [word! any-function! object!]
]

trace: native [
	{Enables and disables evaluation tracing and backtrace.}
	mode [integer! logic!]
	/back {Set mode ON to enable or integer for lines to display}
	/function {Traces functions only (less output)}
;	/stack {Show stack index}
]

trap: native [
	{Tries to DO a block, trapping error as return value (if one is raised).}
	block [block!]
	/with "Handle error case with code"
	handler [block! any-function!] {If FUNCTION!, spec allows [error [error!]]}
]

unless: native [
	{If FALSE condition, return arg; evaluate blocks by default.}
	condition
	false-branch [any-value!]
	/only "Return block arg instead of evaluating it."
]

until: native [
	{Evaluates a block until it is TRUE. }
	block [block!]
]

while: native [
	{While a condition block is TRUE, evaluates another block.}
	cond-block [block!]
	body-block [block!]
]

;-- Data Natives - nat_data.c

bind: native [
	{Binds words to the specified context.}
	word [block! any-word!] {A word or block (modified) (returned)}
	context [any-word! any-object!] {A reference to the target context}
	/copy {Bind and return a deep copy of a block, don't modify original}
	/only {Bind only first block (not deep)}
	/new {Add to context any new words found}
	/set {Add to context any new set-words found}
]

unbind: native [
	{Unbinds words from context.}
	word [block! any-word!] {A word or block (modified) (returned)}
	/deep "Process nested blocks"
]

bound?: native [
	{Returns the context in which a word is bound.}
	word [any-word!]
]

collect-words: native [
	"Collect unique words used in a block (used for context construction)."
	block [block!]
	/deep "Include nested blocks"
	/set "Only include set-words"
	/ignore "Ignore prior words"
	words [any-object! block! none!] "Words to ignore"
]

checksum: native [
	{Computes a checksum, CRC, or hash.}
	data [binary!] {Bytes to checksum}
	/part limit {Length of data}
	/tcp {Returns an Internet TCP 16-bit checksum}
	/secure {Returns a cryptographically secure checksum}
	/hash {Returns a hash value}
	size [integer!] {Size of the hash table}
	/method {Method to use}
	word [word!] {Methods: SHA1 MD5 CRC32}
	/key {Returns keyed HMAC value}
	key-value [any-string!] {Key to use}
]

compress: native [
	{Compresses a string series and returns it.}
	data [binary! string!] {If string, it will be UTF8 encoded}
	/part limit {Length of data (elements)}
	/gzip {Use GZIP checksum}
	/only {Do not store header or envelope information ("raw")}
]

;-- !!! This used to use /PART LENGTH, but when LENGTH? was migrate to LENGTH
;-- most routines taking a /PART changed their parameter name to LIMIT.  This
;-- routine has a /LIMIT refinement that conflicts, however.  Renamed LENGTH
;-- to LIM for the moment, but this interface should be given a review for
;-- naming, in addition to what it means to decompress a /PART of the input.
decompress: native [
	{Decompresses data. Result is binary.}
	data [binary!] {Data to decompress}
	/part lim {Length of compressed data (must match end marker)}
	/gzip {Use GZIP checksum}
	/limit size {Error out if result is larger than this}
	/only {Do not look for header or envelope information ("raw")}
]

construct: native [
	{Creates an object with scant (safe) evaluation.}
	block [block! string! binary!] "Specification (modified)"
	/with "Default object" object [object!]
	/only "Values are kept as-is"
]

debase: native [
	{Decodes binary-coded string (BASE-64 default) to binary value.}
	value [binary! string!] {The string to decode}
	/base {Binary base to use}
	base-value [integer!] {The base to convert from: 64, 16, or 2}
]

enbase: native [
	{Encodes a string into a binary-coded string (BASE-64 default).}
	value [binary! string!] {If string, will be UTF8 encoded}
	/base {Binary base to use}
	base-value [integer!] {The base to convert to: 64, 16, or 2}
]

decloak: native [
	{Decodes a binary string scrambled previously by encloak.}
	data [binary!] "Binary series to descramble (modified)"
	key [string! binary! integer!] "Encryption key or pass phrase"
	/with "Use a string! key as-is (do not generate hash)"
]

encloak: native [
	{Scrambles a binary string based on a key.}
	data [binary!] "Binary series to scramble (modified)"
	key [string! binary! integer!] "Encryption key or pass phrase"
	/with "Use a string! key as-is (do not generate hash)"
]

deline: native [
	"Converts string terminators to standard format, e.g. CRLF to LF."
	string [any-string!] {(modified)}
	/lines "Return block of lines (works for LF, CR, CR-LF endings) (no modify)"
]

enline: native [
	"Converts string terminators to native OS format, e.g. LF to CRLF."
	series [any-string! block!] {(modified)}
]

detab: native [
	"Converts tabs to spaces (default tab size is 4)."
	string [any-string!] {(modified)}
	/size  "Specifies the number of spaces per tab"
	number [integer!]
]

entab: native [
	"Converts spaces to tabs (default tab size is 4)."
	string [any-string!] {(modified)}
	/size "Specifies the number of spaces per tab"
	number [integer!]
]

delect: native [
	"Parses a common form of dialects. Returns updated input block."
	 dialect [object!] "Describes the words and datatypes of the dialect"
	 input [block!] "Input stream to parse"
	 output [block!] "Resulting values, ordered as defined (modified)"
	 /in "Search for var words in specific objects (contexts)"
	 where [block!] "Block of objects to search (non objects ignored)"
	 /all "Parse entire block, not just one command at a time"
]

difference: native [
	{Returns the special difference of two values.}
	set1 [block! string! binary! bitset! date! typeset!] "First data set"
	set2 [block! string! binary! bitset! date! typeset!] "Second data set"
	/case {Uses case-sensitive comparison}
	/skip {Treat the series as records of fixed size}
	size [integer!]
]

exclude: native [
	{Returns the first data set less the second data set.}
	set1 [block! string! binary! bitset! typeset!] "First data set"
	set2 [block! string! binary! bitset! typeset!] "Second data set"
	/case {Uses case-sensitive comparison}
	/skip {Treat the series as records of fixed size}
	size [integer!]
]

intersect: native [
	{Returns the intersection of two data sets.}
	set1 [block! string! binary! bitset! typeset!] "first set"
	set2 [block! string! binary! bitset! typeset!] "second set"
	/case {Uses case-sensitive comparison}
	/skip {Treat the series as records of fixed size}
	size [integer!]
]

union: native [
	{Returns the union of two data sets.}
	set1 [block! string! binary! bitset! typeset!] "first set"
	set2 [block! string! binary! bitset! typeset!] "second set"
	/case {Use case-sensitive comparison}
	/skip {Treat the series as records of fixed size}
	size [integer!]
]

unique: native [
	{Returns the data set with duplicates removed.}
	set1 [block! string! binary! bitset! typeset!]
	/case  {Use case-sensitive comparison (except bitsets)}
	/skip {Treat the series as records of fixed size}
	size [integer!]
]

lowercase: native [
	"Converts string of characters to lowercase."
	string [any-string! char!] {(modified if series)}
	/part {Limits to a given length or position}
	limit [any-number! any-string!]
]

uppercase: native [
	"Converts string of characters to uppercase."
	string [any-string! char!] {(modified if series)}
	/part {Limits to a given length or position}
	limit [any-number! any-string!]
]

dehex: native [
	{Converts URL-style hex encoded (%xx) strings.}
	value [any-string!] {The string to dehex}
]

get: native [
	{Gets the value of a word or path, or values of an object.}
	word {Word, path, object to get}
	/any {Allows word to have no value (allows unset)}
]

in: native [
	{Returns the word or block in the object's context.}
	object [any-object! block!]
	word [any-word! block! paren!]  {(modified if series)}
]

parse: native [
	{Parses a string or block series according to grammar rules.}
	input [any-series!] {Input series to parse}
	;rules [block!] {Rules to parse by}
	; !!! Does not actually handle STRING! and NONE!, used to give a more
	; informative error message directing people to use SPLIT instead
	rules [block! string! none!] {Rules to parse by}
	/case {Uses case-sensitive comparison}
	/all {(ignored refinement left for Rebol2 transitioning)}
]

set: native [
	{Sets a word, path, block of words, or object to specified value(s).}
	word [any-word! any-path! block! object!] {Word, block of words, path, or object to be set (modified)}
	value [any-value!] {Value or block of values}
	/any {Allows setting words to any value, including unset}
	/pad {For objects, if block is too short, remaining words are set to NONE}
]

to-hex: native [
	{Converts numeric value to a hex issue! datatype (with leading # and 0's).}
	value [integer! tuple!] {Value to be converted}
	/size {Specify number of hex digits in result}
	len [integer!]
]

to-integer: native [
	{Synonym of TO INTEGER! when used without refinements, adds /UNSIGNED.}
	value [
		integer! decimal! percent! money! char! time!
		issue! binary! any-string!
	]
	/unsigned {For BINARY! interpret as unsigned, otherwise error if signed.}
]

type-of: native [
	{Returns the datatype of a value.}
	value [any-value!]
]

unset: native [
	{Unsets the value of a word (in its current context.)}
	word [word! block!] {Word or block of words}
]

utf?: native [
	{Returns UTF BOM (byte order marker) encoding; + for BE, - for LE.}
	data [binary!]
]

invalid-utf?: native [
	{Checks UTF encoding; if correct, returns none else position of error.}
	data [binary!]
	/utf "Check encodings other than UTF-8"
	num [integer!] "Bit size - positive for BE negative for LE"
]

value?: native [
	{Returns TRUE if the word has a value.}
	value
]

;-- IO Natives - nat_io.c

print: native [
	{Outputs a value followed by a line break.}
	value [any-value!] {The value to print}
]

prin: native [
	{Outputs a value with no line break.}
	value [any-value!]
]

mold: native [
	{Converts a value to a REBOL-readable string.}
	value [any-value!] {The value to mold}
	/only {For a block value, mold only its contents, no outer []}
	/all  {Use construction syntax}
	/flat {No indentation}
]

form: native [
	{Converts a value to a human-readable string.}
	value [any-value!] {The value to form}
]

new-line: native [
	{Sets or clears the new-line marker within a block or paren.}
	position [block! paren!] {Position to change marker (modified)}
	value {Set TRUE for newline}
	/all {Set/clear marker to end of series}
	/skip {Set/clear marker periodically to the end of the series}
	size [integer!]
]

new-line?: native [
	{Returns the state of the new-line marker within a block or paren.}
	position [block! paren!] {Position to check marker}
]

to-local-file: native [
	{Converts a REBOL file path to the local system file path.}
	path [file! string!]
	/full "Prepends current dir for full path (for relative paths only)"
]

to-rebol-file: native [
	{Converts a local system file path to a REBOL file path.}
	path [file! string!]
]

transcode: native [
	{Translates UTF-8 binary source to values. Returns [value binary].}
	source [binary!] "Must be Unicode UTF-8 encoded"
	/next "Translate next complete value (blocks as single value)"
	/only "Translate only a single value (blocks dissected)"
	/error "Do not cause errors - return error object as value in place"
]

echo: native [
    {Copies console output to a file.}
    target [file! none! logic!]
]

now: native [
	{Returns date and time.}
	/year {Returns year only}
	/month {Returns month only}
	/day {Returns day of the month only}
	/time {Returns time only}
	/zone {Returns time zone offset from UCT (GMT) only}
	/date {Returns date only}
	/weekday {Returns day of the week as integer (Monday is day 1)}
	/yearday {Returns day of the year (Julian)}
	/precise {High precision time}
	/utc {Universal time (no zone)}
]

wait: native [
	{Waits for a duration, port, or both.}
	value [any-number! time! port! block! none!]
	/all {Returns all in a block}
	/only {only check for ports given in the block to this function}
]

wake-up: native [
	{Awake and update a port with event.}
	port [port!]
	event [event!]
]

what-dir: native ["Returns the current directory path."]

change-dir: native [
	"Changes the current path (where scripts with relative paths will be run)."
	path [file! url!]
]

;-- Math Natives - nat_math.c

cosine: native [
	{Returns the trigonometric cosine.}
	value [any-number!] {In degrees by default}
	/radians {Value is specified in radians}
]

sine: native [
	{Returns the trigonometric sine.}
	value [any-number!] {In degrees by default}
	/radians {Value is specified in radians}
]

tangent: native [
	{Returns the trigonometric tangent.}
	value [any-number!] {In degrees by default}
	/radians {Value is specified in radians}
]

arccosine: native [
	{Returns the trigonometric arccosine (in degrees by default).}
	value [any-number!]
	/radians {Returns result in radians}
]

arcsine: native [
	{Returns the trigonometric arcsine (in degrees by default).}
	value [any-number!]
	/radians {Returns result in radians}
]

arctangent: native [
	{Returns the trigonometric arctangent (in degrees by default).}
	value [any-number!]
	/radians {Returns result in radians}
]

exp: native [
	{Raises E (the base of natural logarithm) to the power specified}
	power [any-number!]
]

log-10: native [
	{Returns the base-10 logarithm.}
	value [any-number!]
]

log-2: native [
	{Return the base-2 logarithm.}
	value [any-number!]
]

log-e: native [
	{Returns the natural (base-E) logarithm of the given value}
	value [any-number!]
]

square-root: native [
	{Returns the square root of a number.}
	value [any-number!]
]

shift: native [
	{Shifts an integer left or right by a number of bits.}
	value [integer!]
	bits [integer!] "Positive for left shift, negative for right shift"
	/logical "Logical shift (sign bit ignored)"
]


;-- Conditional logic

and?: native [
	;-- TBD: define infix form later as AND (rename bitwise AND to AND*)
	{Returns true if both values are conditionally true (no "short-circuit")}
	value1
	value2
]

not?: native [
	;-- defined later as synonym NOT
	{Returns the logic complement.}
	value {(Only FALSE and NONE return TRUE)}
]

or?: native [
	;-- TBD: define infix form later as OR (rename bitwise OR to OR+)
	{Returns true if either value is conditionally true (no "short-circuit")}
	value1
	value2
]

xor?: native [
	;-- TBD: define infix form later as XOR (rename bitwise XOR to XOR-)
	{Returns true if only one of the two values is conditionally true.}
	value1
	value2
]


;-- New, hackish stuff:

++: native [
	{Increment an integer or series index. Return its prior value.}
	'word [word!] "Integer or series variable"
]

--: native [
	{Decrement an integer or series index. Return its prior value.}
	'word [word!] "Integer or series variable"
]

stack: native [
	{Returns stack backtrace or other values.}
	offset [integer!] "Relative backward offset"
	/block "Block evaluation position"
	/word "Function or object name, if known"
	/func "Function value"
	/args "Block of args (may be modified)"
	/size "Current stack size (in value units)"
	/depth "Stack depth (frames)"
	/limit "Stack bounds (auto expanding)"
]

resolve: native [
	{Copy context by setting values in the target from those in the source.}
	target [any-object!] {(modified)}
	source [any-object!]
	/only from [block! integer!] "Only specific words (exports) or new words in target (index to tail)"
	/all "Set all words, even those in the target that already have a value"
	/extend "Add source words to the target if necessary"
]

;in-context: native [
;	{Set the default context for global words.}
;	context [object!]
;]

get-env: native [
	{Returns the value of an OS environment variable (for current process).}
	var [any-string! any-word!]
]

set-env: native [
	{Sets the value of an operating system environment variable (for current process).}
	var [any-string! any-word!] "Variable to set"
	value [string!  none!] "Value to set, or NONE to unset it"
]

list-env: native [
	{Returns a map of OS environment variables (for current process).}
]

call: native [
	{Run another program; return immediately.}
	command [string! block! file!] "An OS-local command line (quoted as necessary), a block with arguments, or an executable file"
	/wait "Wait for command to terminate before returning"
	/console "Runs command with I/O redirected to console"
	/shell "Forces command to be run from shell"
	/info "Returns process information object"
	/input in [string! binary! file! none!] "Redirects stdin to in"
	/output out [string! binary! file! none!] "Redirects stdout to out"
	/error err [string! binary! file! none!] "Redirects stderr to err"
]

browse: native [
	{Open web browser to a URL or local file.}
	url [url! file! none!]
]

evoke: native [
	{Special guru meditations. (Not for beginners.)}
	chant [word! block! integer!] "Single or block of words ('? to list)"
]

request-file: native [
	{Asks user to select a file and returns full file path (or block of paths).}
	/save "File save mode"
	/multi "Allows multiple file selection, returned as a block"
	/file name [file!] "Default file name or directory"
	/title text [string!] "Window title"
	/filter list [block!] "Block of filters (filter-name filter)"
]

ascii?: native [
	{Returns TRUE if value or string is in ASCII character range (below 128).}
	value [any-string! char! integer!]
]

latin1?: native [
	{Returns TRUE if value or string is in Latin-1 character range (below 256).}
	value [any-string! char! integer!]
]

infix?: native [
	{Returns TRUE if the function gets its first argument prior to the call}
	value [any-function!]
]

; Temps...

stats: native [
	{Provides status and statistics information about the interpreter.}
	/show {Print formatted results to console}
	/profile {Returns profiler object}
	/timer {High resolution time difference from start}
	/evals {Number of values evaluated by interpreter}
	/dump-series pool-id [integer!] {Dump all series in pool pool-id, -1 for all pools}
]

do-codec: native [
	{Evaluate a CODEC function to encode or decode media types.}
	handle [handle!] "Internal link to codec"
	action [word!] "Decode, encode, identify"
	data [binary! image! string!]
]

access-os: native [
	{Access to various operating system functions (getuid, setuid, getpid, kill, etc.)}
	field [word!] "uid, euid, gid, egid, pid"
	/set "To set or kill pid (sig 15)"
	value [integer! block!] "Argument, such as uid, gid, or pid (in which case, it could be a block with the signal no)"
]

set-scheme: native [
	"Low-level port scheme actor initialization."
	scheme [object!]
]

load-extension: native [
	"Low level extension module loader (for DLLs)."
	name [file! binary!] "DLL file or UTF-8 source"
	/dispatch "Specify native command dispatch (from hosted extensions)"
	function [handle!] "Command dispatcher (native)"
]

do-commands: native [
	"Evaluate a block of extension module command functions (special evaluation rules.)"
	commands [block!] "Series of commands and their arguments"
]

ds: native ["Temporary stack debug"]
dump: native ["Temporary debug dump" v]
check: native ["Temporary series debug check" val [any-series!]]

do-callback: native [
	"Internal function to process callback events."
	event [event!] "Callback event"
]


limit-usage: native [
	"Set a usage limit only once (used for SECURE)."
	field [word!] "eval (count) or memory (bytes)"
	limit [any-number!]
]

selfless?: native [
    "Returns true if the context doesn't bind 'self."
    context [any-word! any-object!] "A reference to the target context"
]

map-event: native [
	"Returns event with inner-most graphical object and coordinate."
	event [event!]
]

map-gob-offset: native [
	"Translates a gob and offset to the deepest gob and offset in it, returned as a block."
	gob [gob!] "Starting object"
	xy [pair!] "Staring offset"
	/reverse "Translate from deeper gob to top gob."
]

as-pair: native [
	"Combine X and Y values into a pair."
	x [any-number!]
	y [any-number!]
]

;read-file: native [f [file!]]

equal?: native [
	{Returns TRUE if the values are equal.}
	value1 [any-value!]
	value2 [any-value!]
]

not-equal?: native [
	{Returns TRUE if the values are not equal.}
	value1 [any-value!]
	value2 [any-value!]
]

equiv?: native [
	{Returns TRUE if the values are equivalent.}
	value1 [any-value!]
	value2 [any-value!]
]

not-equiv?: native [
	{Returns TRUE if the values are not equivalent.}
	value1 [any-value!]
	value2 [any-value!]
]

strict-equal?: native [
	{Returns TRUE if the values are strictly equal.}
	value1 [any-value!]
	value2 [any-value!]
]

strict-not-equal?: native [
	{Returns TRUE if the values are not strictly equal.}
	value1 [any-value!]
	value2 [any-value!]
]

same?: native [
	{Returns TRUE if the values are identical.}
	value1 [any-value!]
	value2 [any-value!]
]

greater?: native [ ; Note: some datatypes expect >, <, >=, <= to be in this order.
	{Returns TRUE if the first value is greater than the second value.}
	value1 value2
]

greater-or-equal?: native [
	{Returns TRUE if the first value is greater than or equal to the second value.}
	value1 value2
]

lesser?: native [
	{Returns TRUE if the first value is less than the second value.}
	value1 value2
]

lesser-or-equal?: native [
	{Returns TRUE if the first value is less than or equal to the second value.}
	value1 value2
]

minimum: native [
	{Returns the lesser of the two values.}
	value1 [any-scalar! date! any-series!]
	value2 [any-scalar! date! any-series!]
]

maximum: native [ ; Note: Some datatypes expect all binary ops to be <= this
	{Returns the greater of the two values.}
	value1 [any-scalar! date! any-series!]
	value2 [any-scalar! date! any-series!]
]

negative?: native [
	{Returns TRUE if the number is negative.}
	number [any-number! money! time! pair!]
]

positive?: native [
	{Returns TRUE if the value is positive.}
	number [any-number! money! time! pair!]
]

zero?: native [
	{Returns TRUE if the value is zero (for its datatype).}
	value
]

;-- Expectation is that evaluation ends in UNSET!, empty parens makes one
()
