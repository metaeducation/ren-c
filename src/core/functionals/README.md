# "FUNCTIONALS" - Functions used to create other functions

This embraces the computer-science term "functional" for being ["synonymous
with higher-order functions, i.e. functions that take functions as arguments
or return them."][1]

[1]: https://en.wikipedia.org/wiki/Functional_(mathematics)

R3-Alpha had a [finite enumerated type][2] for its function types, each of
which manifested as a unique Rebol type (in the typeset ANY-FUNCTION!).  Only
two of these could have instances created by users (MAKE FUNCTION!, which was
used by FUNC and FUNCTION, and MAKE CLOSURE! which was used by CLOS and
CLOSURE).  Rebol2 also had ROUTINE! for FFI, and new instances could be made.

[2]: https://github.com/rebol/rebol/blob/25033f897b2bd466068d7663563cd3ff64740b94/src/core/t-function.c#L127

Ren-C rethinks this idea to have one executable ACTION! type, which gets its
implementation from a "Dispatcher" C function.  As in R3-Alpha, there is an
array which serves as the identity of an action, while also holding an ordered
list of its parameters (a "paramlist").  But a second array is linked to from
an ACTION! cell which holds instance information for that action...and this
"details" array node holds the dispatcher which interprets it when the action
is invoked.

(See %sys-action.h for more information on this subject)

This provides a pattern for adding new function categories easily to the core,
or in extensions--without creating a new "Rebol type" in the process.  See the
`Make_Action()` routine for how this is applied.

Of particular interest to Ren-C is creating actions that are slight variants
of other actions, without having to repeat their interface.  These variants
are also designed to be efficient by reusing the same stack frame, avoiding
proxying parameters to a new call.  Specializations, chains, and enclosures
are just some of these tools...which the system relies on heavily.

## Notes

* The "details" array can be any length, but has an optimal size efficiency
  when it's just one value--because it fits in the series node.  See the
  `Alloc_Singular()` routine for how this works.

* Space for the "details" array in the ACTION! cell was made by eliminating
  the idea that the core holds onto the "spec" block of a function.  Instead,
  storing information about the parameters of a function is optional, and
  goes in what is called the `adjunct` object...which can contain arbitrary
  out-of-band information added onto ACTION!s.  HELP reads this information,
  and it is possible to use variants of functions that do not generate it,
  as well as to free the HELP data after-the-fact.

## Issues

* Due to the variance in implementations for ACTION!, there is no consistent
  mechanism for getting a function's "source".  To be generalized, there
  would have to be some additional hook registered which would know how to
  translate a details array for each dispatcher into some source equivalent
  if that were possible.  (Natives, for instance, may not be possible to
  express in this way... so having the source say this in a comment would
  probably be the best it could provide.)
