# Labeled Sections: How Revolt's C Functions Are Laid Out

Many functions in the Revolt core are written as a series of named, braced
sections. At the time of writing the style appears in 61 source files and
accounts for more than 800 section headers. This page describes the
convention and the reasons for it.

## The Shape

```c
int Some_Function(...) {
    ...
  some_label: {

  // Comments applicable to this section

    ...
    goto yet_another_label;

} some_other_label: {  ///////////////////////////////////////////////////////

  // Comments applicable to this section

    ...

} more_label: {

    ...

} yet_another_label: {  //////////////////////////////////////////////////////

    ...
}}
```

## The Rules

1. **Every section is a label followed by a brace block.** The label is
   indented two spaces. The code inside is indented four, the same as the
   rest of the function body.

2. **One section closes and the next opens on the same line:**
   `} next_label: {`. The last section closes the function too, which is
   why functions in this style end in `}}`.

3. **A divider of slashes means the section is a `goto` target.** A section
   with no divider is only ever entered by falling into it from the section
   above. So the dividers show at a glance where control can arrive from
   somewhere else.

4. **Sections are named even when nothing jumps to them.** The name is
   documentation. This requires turning off the compiler's unused-label
   warning (`-Wno-unused-label` for GCC and Clang, C4102 for MSVC), which
   the build does deliberately.

5. **Comments about a section go at its top, indented two spaces** to line up
   with the label. Numbered remarks (`// 1.`, `// 2.`) are written there and
   referenced from the code below as `// [1]`. Lettered references such as
   `[A]` point to the notes at the top of the file.

6. **A section may contain a nested section**, opened by a label inside it.
   The nested section's closing brace is doubled up with its parent's.

## Use With State Machines

The evaluator is stackless: a native that needs something evaluated returns
to the trampoline and is called again later with a `STATE` byte saying where
it left off. Labeled sections map onto this directly. The function opens
with a `switch` that only dispatches:

```c
DECLARE_NATIVE(ALSO)
{
    INCLUDE_PARAMS_OF_ALSO;

    Value* left = Possibly_Unstable(ARG(LEFT));
    Element* branch = ARG(BRANCH);

    enum {
        ST_ALSO_INITIAL_ENTRY = STATE_0,
        ST_ALSO_RUNNING_BRANCH
    };

    switch (STATE) {
      case ST_ALSO_INITIAL_ENTRY:
        goto initial_entry;

      case ST_ALSO_RUNNING_BRANCH:
        goto discard_branch_result_in_out_and_return_input;

      default: dead_end;
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Is_Failure(left) or Is_Hot_Potato(left))
        return COPY_TO_OUT(left);

    STATE = ST_ALSO_RUNNING_BRANCH;
    return CONTINUE_BRANCH(branch, left);

} discard_branch_result_in_out_and_return_input: {  //////////////////////////

    UNUSED(SUBOUT);  // would corrupt the OUT pointer itself
    Drop_Level(SUBLEVEL);

    return COPY_TO_OUT(left);
}}
```

Each state has one section, and the section's name says what is true on
arrival (`branch_result_in_out`, `predicate_result_in_topout`).

## Why

**The braces make each section a scope.** A variable declared in one section
does not exist in the next. This has three consequences.

- *The code compiles as C++.* C allows a `goto` to jump over a declaration
  that has an initializer; C++ does not. Because every section's variables
  end at its closing brace, no jump between sections can cross a live
  initialization. Revolt is built as C++ for checking, and this style is what
  lets it use `goto` freely there.

- *The compiler enforces the stackless discipline.* When a native returns to
  the trampoline, its C locals are gone. Anything that must survive has to
  live in the Level's cells. A local declared in the `initial_entry` section
  cannot be named in the section that runs after the continuation, so reading
  a value that no longer exists is a compile error, not a bug to be found
  later.

- *Variables can be declared where they are first needed*, close to their
  use, without becoming visible to the rest of a long function.

**The names carry what comments usually carry.** A reader can follow the
`switch` to a label and know both where execution resumes and what state it
is in.

**The sections give the notes somewhere to go.** Each numbered remark sits
at the head of the section it explains, so the code lines stay short and
the reasoning stays next to the code.

## Things To Watch

- **Falling through is silent.** A section that does not end in `goto` or
  `return` runs on into the next one. That is intended, and rule 3 marks
  sections that are *only* entered that way. A section with a divider may
  still be entered by fall-through as well as by `goto`.

- **Note numbers are not checked.** A `[2]` in the code is only correct if
  the section's remarks still have a second item. Renumber the references
  when the remarks change.
