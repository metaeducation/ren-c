Rebol [
   title: "PANIC with an error"
   description: --[
       Used by GitHub Actions to make sure PANIC gives a nonzero exit code.
   ]--
]

print "PANIC-ing with a message (should give exit code 1)"
panic "The Error Message"
