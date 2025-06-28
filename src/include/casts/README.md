# cast() Validation Hooks for Ren-C Types

In C, `cast(Type, expression)` macros just turn into ordinary C parentheses
casts, e.g. `(Type)(expression)`.  But if you build the codebase as C++, then
these casts can do powerful checking, at both compile-time and runtime...

* You can put in rules that prohibit nonsensical casts at compile-time.  This
  means you can catch things like casting from a Cell* to a Strand*, or
  from an int* to a Base*.

* For casts that are allowed to compile, you can add runtime instrumentation
  that validates that the bit patterns in the pointed-to data are legitimate
  for the conversion.  So if you cast from an Atom* to an Element*, it can
  check to make sure that the LIFT_BYTE() of that Cell is not an antiform.

* The rules and checks you make can be done with patterns, vs. having to list
  everything out manually.  The compiler can ask questions of the datatypes
  to see if they fit rules, and these rules can be extended as you need.

See the definition of CastHook<> in the Needful library for documentation
and explanations of the usage.

## **DON'T BE AFRAID, C++ IS HERE TO HELP**

REMEMBER: *The interpreter is written as C, looks like C, and will always
build fine without this.*  These casts are selectively compiled in when
DEBUG_CHECK_CASTS is enabled, and provide powerful mechanisms for enforcing
consistency at compile-time and runtime.

The best way to think of these files is as a kind of "third-party tool", sort
of like Valgrind or Address Sanitizer.  Every effort has been made to make
it as clear as possible, so you can see the C code embedded right into it
which does the powerful checks.

These checks are how Ren-C manages to stay robust and confident in the face
of advanced features and nuanced optimizations.

## If You Wonder How It Works...

The mechanisms used involve things like partial template specialization and
SFINAE ("Substitution failure is not an error"):

   https://en.cppreference.com/w/cpp/language/partial_specialization
   https://en.cppreference.com/w/cpp/language/sfinae

It's actually pretty tame and "easy"-to-grok when compared to a lot of C++
boost or standard library code.  So don't tune out just because it has the
words `template` and `typename` in it!
