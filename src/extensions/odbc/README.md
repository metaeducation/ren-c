ODBC is an abstraction layer for communicating with databases, originating
from Microsoft in the 1990s but commonly available on Linux and other
platforms as well:

https://en.wikipedia.org/wiki/Open_Database_Connectivity

Integration with ODBC was a commercial feature of Rebol2/Command:

http://www.rebol.com/docs/database.html 

Though it was not included in R3-Alpha, Christian Ensel published code to
interface with "hostkit" to provide some of the functionality:

https://github.com/gurzgri/r3-odbc/

That code was taken as the starting point for developing an ODBC extension
against the modern API.
