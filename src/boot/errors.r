REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Error objects"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        Specifies error categories and default error messages.
    }
]

Special: [
    code: 0
    type: "special"     ; Not really "errors"
    null:               {invalid error code zero}
    halt:               {halted by user or script}
]

Internal: [
    code: 1000
    type: "internal"

    ; Because adding an error code has the overhead of modifying this file and
    ; coming up with a good name, errors for testing or that you don't think
    ; will happen can use RE_MISC.  A debug build will identify the line
    ; number and file source of the error, so provides some info already.
    ;
    misc:               {RE_MISC error (if actually happens, add to %errors.r)}

    ; !!! Should there be a distinction made between different kinds of
    ; stack overflows?  (Call stack, Data stack?)
    ;
    stack-overflow:     {stack overflow}

    not-done:           {reserved for future use (or not yet implemented)}

    no-memory:          [{not enough memory:} :arg1 {bytes}]

    io-error:           {problem with IO}
    locked-series:      {locked series expansion}
    unexpected-case:    {no case in switch statement}
    invalid-datatype:   [{invalid datatype #} :arg1]
    bad-path:           [{bad path:} :arg1]
    not-here:           [:arg1 {not supported on your system}]
    globals-full:       {no more global variable space}
    bad-sys-func:       [{invalid or missing system function:} :arg1]
    invalid-error:      [{error object or fields were not valid:} :arg1]
    hash-overflow:      {Hash ran out of space}
    no-print-ptr:       {print is missing string pointer}

    bad-utf8:           {invalid UTF-8 byte sequence found during decoding}
    codepoint-too-high: [{codepoint} :arg1 {too large (or data is not UTF-8)}]

    debug-only:         {Feature available only in DEBUG builds}

    host-no-breakpoint: {Interpreter host code has no breakpoint handler}
    no-current-pause:   {No current PAUSE or BREAKPOINT instruction in effect}

    invalid-exit:       {Frame does not exist on the stack to EXIT from}
    out-of-error-numbers: {There is no more base error code available}
]

Syntax: [
    code: 2000
    type: "syntax error"
    invalid:            [{invalid} :arg1 {--} :arg2]
    missing:            [{missing} :arg2 {at} :arg1]
    no-header:          [{script is missing a REBOL header:} :arg1]
    bad-header:         [{script header is not valid:} :arg1]
    bad-checksum:       [{script checksum failed:} :arg1]
    malconstruct:       [{invalid construction spec:} :arg1]
    bad-char:           [{invalid character in:} :arg1]
    needs:              [{this script needs} :arg1 :arg2 {or better to run correctly}]
]

Script: [
    code: 3000
    type: "script error"

    no-value:           [:arg1 {has no value}]
    need-value:         [:arg1 {needs a value}]
    not-bound:          [:arg1 {word is not bound to a context}]
    no-relative:        [:arg1 {word is bound relative to context not on stack}]
    not-in-context:     [:arg1 {is not in the specified context}]

    no-arg:             [:arg1 {is missing its} :arg2 {argument}]
    expect-arg:         [:arg1 {does not allow} :arg2 {for its} :arg3 {argument}]
    arg-required:       [:arg1 {requires} :arg2 {argument to not be void}]
    expect-val:         [{expected} :arg1 {not} :arg2]
    expect-type:        [:arg1 :arg2 {field must be of type} :arg3]
    cannot-use:         [{cannot use} :arg1 {on} :arg2 {value}]

    do-running-frame:   [{Must COPY a FRAME! that's RUNNING? before DOing it}]
    do-expired-frame:   [{Cannot DO a FRAME! whose stack storage expired}]

    multiple-do-errors: [{DO-ALL encountered multiple failures} :arg1 :arg2]

    apply-too-many:     {Too many values in processed argument block of APPLY.}
    apply-has-changed:  {APPLY takes frame def block (or see r3-alpha-apply)}
    apply-non-function: [:arg1 {needs to be a function for APPLY/SPECIALIZE}]

    invalid-tighten:    {TIGHTEN does not support SPECIALIZE/ADAPT/CHAIN}
    print-needs-eval:   {PRINT needs /EVAL to process non-literal blocks}

    hijack-blank:       {Hijacked function was captured but no body given yet}

    expression-barrier: {Expression barrier hit while processing arguments}
    bar-hit-mid-case:   {Expression barrier hit in middle of CASE pairing}
    enfix-quote-late:   [:arg1 {can't left quote a forward quoted value}]
    partial-lookback:   [:arg1 {can't complete} :arg2 {expression on left}]

    enfix-path-group:   [:arg1 {GROUP! can't be in a lookback quoted PATH!}]

    hard-quote-void:    [:arg1 {is hard quoted and can't be optionally void}]

    reduce-made-void:   {Expression in REDUCE evaluated to void}
    break-not-continue: {Use BREAK/WITH when body is the breaking condition}

    ; !!! Temporary errors while faulty constructs are still outstanding
    ; (more informative than just saying "function doesn't take that type")
    use-eval-for-eval:  {Use EVAL or APPLY to call functions arity > 0, not DO}
    use-fail-for-error: [{Use FAIL (not THROW or DO) to raise} :arg1]
    use-split-simple:   {Use SPLIT (instead of PARSE) for "simple" parsing}

    limited-fail-input: {FAIL requires complex expressions to be in a GROUP!}

    invalid-arg:        [{invalid argument:} :arg1]
    invalid-type:       [:arg1 {type is not allowed here}]
    invalid-op:         [{invalid operator:} :arg1]
    no-op-arg:          [:arg1 {operator is missing an argument}]
    invalid-data:       [{data not in correct format:} :arg1]
    not-same-type:      {values must be of the same type}
    not-related:        [{incompatible argument for} :arg1 {of} :arg2]
    bad-func-def:       [{invalid function definition:} :arg1]
    bad-func-arg:       [{function argument} :arg1 {is not valid}] ; can be a number

    needs-return-value: [:arg1 {must return value (use PROC or RETURN: <opt>)}]
    bad-return-type:    [:arg1 {doesn't have RETURN: enabled for} :arg2]

    no-refine:          [:arg1 {has no refinement called} :arg2]
    bad-refines:        {incompatible or invalid refinements}
    bad-refine:         [{incompatible or duplicate refinement:} :arg1]
    argument-revoked:   [:arg1 {refinement revoked, cannot supply} :arg2]
    bad-refine-revoke:  [:arg1 {refinement in use, can't be revoked by} :arg2]
    non-logic-refine:   [:arg1 {refinement must be LOGIC!, not} :arg2]
    refinement-arg-opt: [{refinement arguments cannot be <opt>}]

    invalid-path:       [{cannot access} :arg2 {in path} :arg1]
    bad-path-type:      [{path} :arg1 {is not valid for} :arg2 {type}]
    bad-path-set:       [{cannot set} :arg2 {in path} :arg1]
    bad-field-set:      [{cannot set} :arg1 {field to} :arg2 {datatype}]
    dup-vars:           [{duplicate variable specified:} :arg1]

    past-end:           {out of range or past end}
    missing-arg:        {missing a required argument or refinement}
    too-short:          {content too short (or just whitespace)}
    too-long:           {content too long}
    invalid-chars:      {contains invalid characters}
    invalid-compare:    [{cannot compare} :arg1 {with} :arg2]

    verify-void:        [{verification condition void at:} :arg1]
    verify-failed:      [{verification failed for:} :arg1]

    invalid-part:       [{invalid /part count:} :arg1]

    no-return:          {block did not return a value}
    block-lines:        {expected block of lines}
    no-catch:           [{Missing CATCH for THROW of} :arg1]
    no-catch-named:     [{Missing CATCH for THROW of} :arg1 {with /NAME:} :arg2]

    bad-bad:            [:arg1 {error:} :arg2]

    bad-make-arg:       [{cannot MAKE/TO} :arg1 {from:} :arg2]
;   no-decode:          [{cannot decode} :arg1 {encoding}]
    wrong-denom:        [:arg1 {not same denomination as} :arg2]
;   bad-convert:        [{invalid conversion value:} :arg1]
    bad-compression:    [{invalid compressed data - problem:} :arg1]
    dialect:            [{incorrect} :arg1 {dialect usage at:} :arg2]
    bad-command:        {invalid command format (extension function)}

    return-archetype:   {RETURN called with no generator providing it in use}
    leave-archetype:    {LEAVE called with no generator providing it in use}

    parse-rule:         {PARSE - invalid rule or usage of rule}
    parse-end:          {PARSE - unexpected end of rule}
    parse-variable:     [{PARSE - expected a variable, not:} :arg1]
    parse-command:      [{PARSE - command cannot be used as variable:} :arg1]
    parse-series:       [{PARSE - input must be a series:} :arg1]

    not-ffi-build:      {This Rebol build wasn't linked with libffi features}
    not-tcc-build:      {This Rebol build wasn't linked with libtcc features}
    bad-library:        {bad library (already closed?)}
    only-callback-ptr:  {Only callback functions may be passed by FFI pointer}
    free-needs-routine: {Function to destroy struct storage must be routine}

    block-skip-wrong:   {Block is not even multiple of skip size}
;   bad-prompt:         [{Error executing prompt block}]
;   bad-port-action:    [{Cannot use} :arg1 {on this type port}]
;   face-error:         [{Invalid graphics face object}]
;   face-reused:        [{Face object reused (in more than one pane):} :arg1]

    frame-already-used: [{Frame currently in use by a function call} :arg1]
    frame-not-on-stack: {Frame is no longer running on the stack}

    recursive-varargs:  {VARARGS! chained into itself (maybe try <durable>?)}
    varargs-no-stack:   {Call originating VARARGS! has finished running}
    varargs-make-only:  {MAKE *shared* BLOCK! supported on VARARGS! (not TO)}
    varargs-no-look:    {VARARGS! may only lookahead by 1 if "hard quoted"}
    varargs-take-last:  {VARARGS! does not support TAKE-ing only /LAST item}

    map-key-unlocked:   [{key must be LOCK-ed to add to MAP!} :arg1]
    tcc-not-supported-opt: [{Option} :arg1 {is not supported}]
    tcc-expect-word:     [{Option expecting a word:} :arg1]
    tcc-invalid-include: [{Include expects a block or a path:} :arg1]
    tcc-invalid-options: [{Options expect string} :arg1]
    tcc-invalid-library: [{Library expects a block or a path:} :arg1]
    tcc-invalid-library-path: [{Library path expects a block or a path:} :arg1]
    tcc-invalid-runtime-path: [{Runtime library path expects a block or a path:} :arg1]
    tcc-empty-spec:    	{Spec for natives must not be empty}
    tcc-empty-source:    {Source for natives must not be empty}
    tcc-construction:    {TCC failed to create a TCC context}
    tcc-set-options:     {TCC failed to set TCC options}
    tcc-include:    	 [{TCC failed to add include path:} :arg1]
    tcc-library:    	 [{TCC failed to add library:} :arg1]
    tcc-library-path:    [{TCC failed to add library path:} :arg1]
    tcc-runtime-path:    [{TCC failed to add runtime library path:} :arg1]
    tcc-output-type:     {TCC failed to set output to memory}
    tcc-compile:    	 [{TCC failed to compile the code} :arg1]
    tcc-relocate:    	 {TCC failed to relocate the code}
    tcc-invalid-name:    [{C name must be a string:} :arg1]
    tcc-sym-not-found:   [{TCC failed to find symbol:} :arg1]
    tcc-error-warn:      [{TCC reported error/warnings. Fix error/warnings, or use '-w' to disable all of the warnings:} :arg1]

    block-conditional:  [{Literal block used as conditional} :arg1]
    block-switch:       [{Literal block used as switch value} :arg1]

    non-unloadable-native:    [{Not an unloadable native:} :arg1]
    native-unloaded:    [{Native has been unloaded:} :arg1]
    fail-to-quit-extension:   [{Failed to quit the extension:} :arg1]
]

Math: [
    code: 4000
    type: "math error"

    zero-divide:        {attempt to divide by zero}
    overflow:           {math or number overflow}
    positive:           {positive number required}

    type-limit:         [:arg1 {overflow/underflow}]
    size-limit:         [{maximum limit reached:} :arg1]
    out-of-range:       [{value out of range:} :arg1]
]

Access: [
    code: 5000
    type: "access error"

    protected-word:     [{variable} :arg1 {locked by PROTECT (see UNPROTECT)}]
    
    series-protected:   {series read-only due to PROTECT (see UNPROTECT)}
    series-frozen:      {series is source or permanently locked, can't modify}
    series-running:     {series temporarily read-only for running (DO, PARSE)}

    hidden:             {not allowed - would expose or modify hidden values}

    cannot-open:        [{cannot open:} :arg1 {reason:} :arg2]
    not-open:           [{port is not open:} :arg1]
    already-open:       [{port is already open:} :arg1]
;   already-closed:     [{port} :arg1 {already closed}]
    no-connect:         [{cannot connect:} :arg1 {reason:} :arg2]
    not-connected:      [{port is not connected:} :arg1]
;   socket-open:        [{error opening socket:} :arg1]
    no-script:          [{script not found:} :arg1]

    no-scheme-name:     {Scheme has no `name:` field (must be WORD!)}
    no-scheme:          [{missing port scheme:} :arg1]

    invalid-spec:       [{invalid spec or options:} :arg1]
    invalid-port:       [{invalid port object (invalid field values)}]
    invalid-actor:      [{invalid port actor (must be native or object)}]
    invalid-port-arg:   [{invalid port argument:} :arg1]
    no-port-action:     [{this port does not support:} :arg1]
    protocol:           [{protocol error:} :arg1]
    invalid-check:      [{invalid checksum (tampered file):} :arg1]

    write-error:        [{write failed:} :arg1 {reason:} :arg2]
    read-error:         [{read failed:} :arg1 {reason:} :arg2]
    read-only:          [{read-only - write not allowed:} :arg1]
    timeout:            [{port action timed out:} :arg1]

    no-create:          [{cannot create:} :arg1]
    no-delete:          [{cannot delete:} :arg1]
    no-rename:          [{cannot rename:} :arg1]
    bad-file-path:      [{bad file path:} :arg1]
    bad-file-mode:      [{bad file mode:} :arg1]
;   protocol:           [{protocol error} :arg1]

    security:           [{security violation:} :arg1 { (refer to SECURE function)}]
    security-level:     [{attempt to lower security to} :arg1]
    security-error:     [{invalid} :arg1 {security policy:} :arg2]

    no-codec:           [{cannot decode or encode (no codec):} :arg1]
    bad-media:          [{bad media data (corrupt image, sound, video)}]
;   would-block:        [{operation on port} :arg1 {would block}]
;   no-action:          [{this type of port does not support the} :arg1 {action}]
;   serial-timeout:     {serial port timeout}
    no-extension:       [{cannot open extension:} :arg1]
    bad-extension:      [{invalid extension format:} :arg1]
    extension-init:     [{extension cannot be initialized (check version):} :arg1]

    call-fail:          [{external process failed:} :arg1]

    permission-denied:  [{permission denied}]
    process-not-found:  [{process not found:} :arg1]

    symbol-not-found:   [{symbol not found:} :arg1]
    bad-memory:         [{non-accessible memory at} :arg1 {in} :arg2]
    no-external-storage: [{no external storage in the series}]
    already-destroyed:  [{storage at} :arg1 {already destroyed}]
]

Command: [
    code: 6000
    type: "command error"
    bad-cmd-args:       ["Bad command arguments"]
    no-cmd:             ["No command"]
;   fmt-too-short:      {Format string is too short}
;   fmt-no-struct-size: [{Missing size spec for struct at arg#} :arg1]
;   fmt-no-struct-align: [{Missing align spec for struct at arg#} :arg1]
;   fmt-bad-word:       [{Bad word in format string at arg#} :arg1]
;   fmt-type-mismatch:  [{Type mismatch in format string at arg#} :arg1]
;   fmt-size-mismatch:  [{Size mismatch in format string at arg#} :arg1]
;   dll-arg-count:      {Number of arguments exceeds 25}
;   empty-command:      {Command is empty}
;   db-not-open:        {Database is not open}
;   db-too-many:        {Too many open databases}
;   cant-free:          [{Cannot free} :arg1]
;   nothing-to-free:    {Nothing to free}
;   ssl-error:          [{SSL Error: } :arg1]
    command-fail:       ["Command failed"]
]

; If new category added, be sure to update RE_MAX in %make-boot.r
; (currently RE_COMMAND_MAX because `Command: [...]` is the last category)

; Note that 10000 is the hardcoded constant in %make-boot.r used for RE_USER
