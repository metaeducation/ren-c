This extension is a placeholder for material that is being removed from Rebol
Core and is relevant to a GUI environment.

It is being started by taking the "file request" common dialog code for Windows
and GTK and migrating that out, also cutting out the "API middleman" which
had historically forced it to go through Utf8* and a REBRFR "abstraction".
