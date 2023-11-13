> **Note**: Interactive debugging is a work in progress; R3-Alpha had no
> breakpoint ability or otherwise, so this is all new.

## DEBUGGER EXTENSION

R3-Alpha had a limited tracing facility with `TRACE ON`.  However it had no
capability for stepwise debugging or breakpoints.

Ren-C is attempting to implement a debugger as an extension, where much of
the code is written in usermode and is scriptable.  This is rather complex
because Rebol has a single-threaded model, and it means the debugger
implementation itself has to be careful to avoid interacting with the stack
(e.g. trying to "Step Into" its own implementation, vs. the code the user
is trying to debug.)

### FRAME!

One advancement in Ren-C that empowers the creation of debug features is the
FRAME! datatype.  This is a class of object whose keys are the arguments and
locals of an ACTION!...and an instance is generated on each function call.

### EVALUATOR HOOKS

Another advancement which is important to debugging is the ability to "hook"
the evaluator.  A hook is able to see each step the evaluator takes, as well
as its ultimate result.

The concept is that a hook should be able to see everything it would take to
implement what `TRACE ON` could do in R3-Alpha, without needing to build
tracing directly into the evaluator.  Because it can do that, it should be
able to implement a kind of single-stepping as well.

This idea is a work in progress, presenting several challenges in practice.
However, evaluator development attempts to keep the future needs of debugging
and tracing in mind.
