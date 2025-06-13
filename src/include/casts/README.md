# CastHelper Definitions for Ren-C Types

## **DON'T BE (TOO) AFRAID OF THIS SCARY-LOOKING CODE**

These files contain C++ template metaprogramming that delves into techniques
like partial template specialization and SFINAE ("Substitution failure is
not an error"):

   https://en.cppreference.com/w/cpp/language/partial_specialization
   https://en.cppreference.com/w/cpp/language/sfinae

It's actually pretty tame and "easy"-to-grok when compared to a lot of C++
boost or standard library code.  Though it's quite understandable that a C
programmer would look at it and think it's completely bonkers.  (While
dealing with pathological error messages designing this, I certainly had
thoughts about tossing the whole thing rather than try to keep it working.)

...BUT bear in mind: *The interpreter is written as C, looks like C, and
will always build fine without this.*  These casts are selectively compiled
in when DEBUG_CHECK_CASTS is enabled, and provide powerful mechanisms for
enforcing consistency at compile-time and runtime.

The best way to think of this is as a kind of "third-party tool", sort of
like Valgrind or Address Sanitizer.  While it's not "written in C", C++ is
a superset of C.  So without really understanding the C++ bits, you can
still inject arbitrary C code here to run whenever a `cast(type, value)`
operation executes.  This means that if you have a datatype like Flex or
Cell, you can do runtime validation of the bits in these types when
`cast(Flex*, ptr)` or `cast(Cell*, ptr)` happen.  That's an extremely
useful hook!

Beyond that, you can even stop certain casts from happening at all at
compile-time.  A good example would be casting to a mutable Symbol*, which
should never be possible: Symbol is a String subclass, but all pointers
to Symbol should be const.  (This was tried by making Symbol(const*) make
a smart pointer class in DEBUG_CHECK_CASTS, which disabled Symbol(*)...but
you'll have to take my word for it that this solution is much less
convoluted and much more performant.)

## You Don't Have To Understand It To Use It

Explaining the C++ voodoo to a C programmer is beyond the scope of what can
be accomplished in a short description.  *But you don't need to understand
it to use it.*  If a debugging scenario would benefit from rigging in some
code at the moment datatypes are cast, then just edit the bodies of the
`CastHelper::convert<>` functions and ignore everything else.

# NOTES

## Note [A] - Partial Specializing CastHelper to One Template Parameter 

The main CastHelper template take two parameters: the "value's type (V)"
that is cast from, and the "type (T)" being cast to.  These files are using
template partial specializations that only take one parameter: the `V` type
being cast from.  (e.g. we want to define an operator that does the handling
for casting to an Array*, that gets the arbitrary type being
cast from to inspect).

In order to not get in the way of smart pointer classes, we narrow the
specializations to V* raw pointer types (the smart pointers can overload
CastHelper and extract that raw pointer, then delegate to cast again)

## Note [B] - Need CastHelper Specializations for Both T* and const T*

Due to the general nature of C++ partial template specialization, you have to
write separate specializations for <const Foo*> and <Foo*>, even when your
casting behavior is desired to be largely the same.  This means if you forget
to put some handling (or disablement) of both forms, then you won't get the
hooked/checked cast behavior...and it  will just fall through to the default
reinterpret_cast<> fallback in the CastHelper template definition.

While this leads to some annoying boilerplate to try and pipe both the const
and non-const cases through the same code, it does permit you to do things like
completely prohibit the casting to either of the const or non-const forms, if
they don't make sense.  But generally speaking, the trick is to write the const
version, and then reuse it in the non-const version with a const_cast<> to 
drop the const on the return.

Annoyingly you have to check for casting away const, as there's no clear or
performant way to have that inherited when you are taking over the CastHelper
for a partial specialization.

## Note [C] - Speedy Upcast Path For Trusting The Type System

By default, if you upcast (e.g. casting from a derived class like Array to a
base class like Flex), we do this with a very-low-cost constexpr that does the
cast for free.  This is because every Array is-a Flex, and if you have an
Array* in your hand we can assume you got it through a means that you knew it
was valid.

But if you downcast (e.g. from a Node* to a VarList*), then it's a riskier
operation, so validation code is run:

  https://en.wikipedia.org/wiki/Downcasting

However, this rule can be bent when you need to.  If debugging a scenario and
you suspect corruption is happening in placees an upcast could help locate, just
comment out the optimization and run the checks for all casts.
