//
// toplevel.test.js
//
// Some features of libRebol can't be used from during a rebPromise().
// (Even if they could be, they would still be running under different
// conditions at the top level.)
//
// Getting to what libRebol considers the "top level" cannot happen with
// JS-DO and its implementation of adding a <script> tag to the page.
// That is because JS-DO is an awaiter and must be run in a rebPromise().
// The *entire* REPL session itself runs in a rebPromise() in fact, and
// doesn't exit that promise until you QUIT.
//
// For the moment, these tests are included in the ReplPad boot just as
// that boot is done very often.  The test project should be separate.
//

function toplevelTest() {
    let all_pass = true

    function logTest(id, result) {
       if (result == true)
          console.log("toplevelTest #" + id + " PASSED")
       else {
          if (type != "boolean")
              console.error("toplevelTest #" + id + " was " + result)
          console.log("toplevelTest #" + id + " FAILED")
          all_pass = false
       }
    }

    logTest(1, 3 == reb.UnboxInteger("1 + 2"))

    return all_pass
}
