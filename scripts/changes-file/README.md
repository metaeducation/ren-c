# Make CHANGES.md (for Ren/C repo)

CHANGES.md keeps a list of notable changes to this repo grouped by release and type of change.

This process will automatically create a new CHANGES.md

## Files

Along with this README.md in `/scripts/changes-file/` you will also find:

### changes-file.reb

Rebol module containing all code for this task.

### make-changes-file.reb

Rebol script run to make new CHANGES.md

### cherry-pick-map.reb

A Rebol map which is loaded and used to customise the process.


From top level of this repo the following file are also used:

### CHANGES.md

This is the file of notable changes which will be re-written by this process.

### CREDITS.md

Contains the list of contributors to the project. This is read to work out the Github @username.  If contributor not present then *author* from git log will be used as-is.


## Run script

Using the `r3` binary built (from source) in this repo run the `make-changes-file.reb` script:

    ../../make/r3 make-changes-file.reb

This will now make a new CHANGES.md file with all latest notable changes.

## Requirements

- Git 

NB. Due to bug in `CALL/OUTPUT` a workaround is currently in-place which means it requires a shell for redirection (outputs a %tmp-commit-log file which is then parsed)


## How does it work?

A *pretty format* `git log` is converted into a Rebol block which is then parsed and collated into notable changes grouped by release & type of change.

See Tech Info for more info.

### What makes a change notable?

One of three things:

- Commit summary is prefixed with a "\* " (asterix and space)
- Contains a CureCode reference (CC-1111, CC#1111 or similar)
- Is flagged in `cherry-pick-map.reb`

### How do i flag notable commit in cherry-pick-map?

Either a `yes` or with a `[]` block.

```rebol
"e263431" yes
"fbe5237" [type: 'Add]
```

### So how do i force a notable change to be suppressed?

Use `no` in the `cherry-pick-map.reb`

```rebol
"4c29aae" no
```

NB. Because the `cherry-pick-map.reb` was semi-manually created then in some cases you can just remove the entry for suppression to work.  But a force is required if notable change was marked automatically.


### How does it categorise the type of commit (Added, Changed, etc)?

If the commit summary *leads* with Add(ed), Remove(d), Delete(d), Fix(ed), Patch(ed), Deprecate(d) or Security then the change will be categorised accordingly.

The default is "Changed".  An overriding category type can be set in `cherry-pick-map.reb`:

```rebol
"50e015f" [type: 'Removed]
```

The Changes file has the following types:

- Added (new feature)
- Changed (changes to existing functionality)
- Deprecated (marked for removing in upcoming releases)
- Fixed (bug fix)
- Removed (removed feature or deprecation)
- Security (security fix/change)


### So how do i add some example code to a change?  And trello or wiki link?

Like so in `cherry-pick-map.reb`:

```rebol
"fd5f4d6" [
    example: {
print "Some Rebol code"
}
    trello: https://trello.com/c/4Kg3DZ2H/
    wiki: https://github.com/r3n/reboldocs/wiki/User-and-Console
]
```

### How can i combine a group of changes into one?

If you had the following changes:

```rebol
"922658f" yes   ;; Added REPL object to allow skinning (#475)
"00def28" yes
"2955730" yes
] 
```

Then these three changes can be combined into one like so:

```rebol
"922658f" [
    related: ["00def28" "2955730"]
    summary: "Added CONSOLE! object & skinning"
] 
```

Note change in summary message otherwise it would have used summary provided by 922658f commit. 


### How to mark a version release?

Like so:

```rebol
"08eb7e8" [version: "R3-Alpha"]
```

## Tech info

Some info about internal structures used (blocks).

Example commit-log block (extracted via `git log` - see GET-GIT-LOG func)

```rebol

[
    commit: {b953008} 
    author: {Joe Bloggs} 
    email: {joe.bloggs@example.com}
    date-string: {2017-05-29 16:34:12 +0100} 
    summary: {Console skin update bug fix (#525)}

    ;; above comes over directly from git-log.  
    ;; Below are added by this program (some optional)

    type:  'Changed
    related: [...]  ;; if combined commits then provides links to related commits
    cc: [...]       ;; links to any Rebol issues (Cure-Code)
    wiki: https://github.com/r3n/reboldocs/wiki/User-and-Console        
    trello: https://trello.com/xxxx

    issues: [] ;; PR / Issue codes (CURRENTLY NOT USED)
]
```

Above block will get converted into a Change Object loaded under Changes-block (series)
which is made up like this....

- Changes-block -> Release objects -> Category (type) objects -> Change object

```rebol
[
    make release! [
        version: "Undefined"
        date: _
        changes: [
            make category! [
                Added: make change! [] 
                Changed: make change! []
                ... ;; see category! for full list
            ]
        ]
    ]

    make release! [...]
]
```
