See README.md for project overview and directory structure.

- Run `ninja help` to discover available targets.
- During the edit/build/test loop, use `ninja --quiet smoke` for fast feedback
  (builds and tests one platform preset). Only run `ninja`, `ninja tidy`, and
  `ninja format` once smoke is passing.

## API conventions

- `make_XXX(string_view sv)` must be a dumb wrapper around `XXX::from_chars`:
  call `from_chars`, verify `end == sv.data() + sv.size()`, return result.
  No other logic. This ensures `from_chars` is the single parsing implementation
  and the only entry point that needs fuzz coverage.
