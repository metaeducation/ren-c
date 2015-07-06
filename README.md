![Ren/C logo](https://raw.githubusercontent.com/metaeducation/ren-c/master/ren-c-logo.png)

**Ren/C** (will be) a repackaged and tuned variant of the open sourced
[Rebol](http://en.wikipedia.org/wiki/Rebol) codebase.  Its focus is on
providing the full spectrum of Rebol's interpreter internals to any
executable.

Previous API attempts were somewhat anemic, and came in a pre-open-source
era where every implementation detail of Rebol had to be shielded from
client code.  Ren/C seeks to expose more control over the interpreter so
that an extension has a choice to create something with the powers that
interpreter code itself had.  (This permits implementation of things like
debuggers, for instance.)

As Ren/C is still pending publication at time of writing (20-May-2015)
this README stands as a placeholder.  It is an attempt to try and find
the best framing of the situation for bringing together stakeholders,
so that the plan may be tuned further prior to release.

## RATIONALE

Rebol was released as a closed-source language implementation in
1997.  Its unusual blending of inspirations (from Lisp, Forth, Logo --
plus some new ideas) caught the attention of a number of people.  It
positioned itself as a "rebellion" or "resistance" to programming
practices that allow dependencies and complexity to go unchecked.

Many open-source clones came and went over the years, but none really
had the manpower or seeming level of dedication to produce a true
alternative until Red.  The writing was on the wall that the mindshare
of the Rebol community was shifting its support away from Rebol and
toward Red:

[Red Programming Language](http://www.red-lang.org/p/contributions.html)

In light of that, and with Rebol development stalled, Rebol's original
author moved on to other projects.  While his skepticism of the "sloppy"
products of open-source process was well known, he finally bit the
bullet to publish the source code on GitHub on 12-Dec-2012.

A vocal number of users asked for the GitHub rebol/rebol repository to
be delegated and managed by those with the time to (at minimum) patch
crashing bugs in the interpreter.  This delegation stalled somehow, and
the GitHub rebol/rebol repository appeared lifeless...going for over a
year without any pull-requests responded to.

The situation was untenable, yet no one really wanted to take on the
lone responsibility of declaring a fork and trying to sign people on
board with that fork.  Most held out for an eleventh-hour endorsement
of the path Rebol would take from the "official" voice and owner of
the trademark.  Despite best efforts, that has not (yet) come to pass.

Ren/C tries to get around the issue by not positioning itself as another
brand of executable.  Rather it fixes bugs and architecture in the code
with the hopes that these changes could be taken back into a Rebol
branded interpreter... while many other doors are opened for code that
might use Rebol internally as a scripting engine.

## CONSENSUS

Release of the RenC sources is imminent, but it is in the organizational
phase at the moment.  The goal is to not step on the toes of stakeholders
(when possible) and get people on the same page prior to release.  The
ideal goal would be that RenC be hammered into a core used by all Rebol
forks.  But of particular interest is unity with code maintained by Atronix
Engineering, RebolSource.Net, and ultimately Rebol itself.

If you have been invited as a stakeholder in this project with write
access to the repository, it is not necessarily for your C programming
skillset.  Rather it is a way of asking for your buy-in with a sort of
"paying it forward"...and trusting you with the power to (for instance)
triage and dismiss issues out of the Issue ticketing database.
