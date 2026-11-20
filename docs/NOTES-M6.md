# NOTES-M6

Builder notes for M6 (PLUGIN-PLAN section 4 "M6 Sample mode 8, the Sample tab and Preview" and 7.8). Append only: one
bullet per item, with the reason. Each part writes its own section: T1, T2, T3, Fix.

## T1

The Sample tab's rules and readouts, the full Sample tab and the Macros tab (plan 7.8 T1, decisions 1 and 2). Branch
`m6-sample`, from main 3279614.

### Progress

- Done: `ui/SampleTabRules` (the pure part of sl-ui-sample.js, and the knobs' specs and drag rules).
- Done: the fixtures, appended: `ui/sample-rules.json` gains the whole sound preset x recipe x clip preset product as
  digests; `ui/readouts.json` gains what the real Sample and Macros tabs do, run under the prototype tests' own DOM.
- Done: `Tests/ui/test_sample_rules.cpp` and `test_readouts.cpp` (the pure rows).
- In progress: the Sample tab and the Macros tab in C++, and their tests in the editor.
- Next: CI, the pictures, these notes, the fast-forward of main.
