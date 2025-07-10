; FREE was introduced to manage a memory state where a series has
; outstanding references but its data allocation can be gotten
; rid of anyway.

(
   x: copy "abc"
   free x
   free? x
)(
   x: copy [<a> <b> <c>]
   free x
   free? x
)(
   x: copy #{ABCABC}
   free x
   free? x
)
