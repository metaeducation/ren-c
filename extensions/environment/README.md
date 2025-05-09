# ENVIRONMENT! Extension

This adds the ENVIRONMENT! datatype, which is a psuedo-object that supports the
PICK and POKE method for getting environment variables.  WORD! or TEXT! can be
used for the variable name:

    >> pick env 'PWD
    == "/usr/lib"

    >> pick env "PWD"
    == "/usr/lib"

Supporting PICK and POKE means TUPLE! access comes for free:

    >> env.PWD
    == "/usr/lib/"

Attempting to PICK a variable that's not there will error:

    >> env.SOME_NONEXISTENT_THING
    ** Script Error: cannot pick SOME_NONEXISTENT_THING

But as it's an error, you can say that's okay using TRY:

    >> try env.SOME_NONEXISTENT_THING
    == ~null~  ; anti
