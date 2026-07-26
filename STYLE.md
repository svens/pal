# STYLE

Source formatting for pal. See AGENTS.md for build/test and tooling, README.md
for layout. No auto-formatter; match surrounding code.

## 1. Baseline

1. Whitespace, encoding, line endings: `.editorconfig` is authoritative. Tabs
   for indentation, LF, UTF-8, final newline, no trailing whitespace.
2. ASCII-only sources; escape required non-ASCII in string literals.
3. Tab width 8; keep lines within 120 columns. Unbreakable data (digests, byte
   arrays, base64) may exceed it.
4. Braces: Allman everywhere: functions, types, enums, namespaces, control
   statements, lambda bodies, `requires` expressions.
5. Language: C++23, `snake_case` for all names, types and concepts included.
   Template parameters are the exception: CamelCase, spelled out when naming a
   domain concept (`Protocol`, `Buffers`, `Transport`), a single capital for a
   bare generic (`T`, `F` for a callable, `H` for a handler).

## 2. Constructs

Rules covering cases the baseline does not.

### 2.1 Spacing

1. A space precedes `(` in a function/method declaration or definition and after
   `if`/`for`/`while`/`switch`. None in a call. `operator X` is a function name,
   so it takes the space too (`operator== (`, `operator-> ()`). None between a
   capture list and its parameters (`[&](auto h)`).
2. A pointer/reference binds to the name: `value_type *head`.
3. No space before an inheritance or enum-base colon (`enum class errc: int`).

### 2.2 Layout

1. One blank line after an opening `namespace` brace and after each access
   specifier; access specifiers sit at the class's own indentation. Close a
   namespace with nothing but its `// namespace[ name]` comment.
2. Group related one-liners (special members, alias sets) with no blank line;
   separate multi-line members with one blank line.

### 2.3 Line breaking

1. Break only to stay within the column limit. Break after the opening `(`, one
   argument per line, closing `)` on its own line. Exception: a function
   definition joins `)` to the last parameter so the brace stands alone.
2. Lead a continuation with its operator; one operand per line in a broken chain.
   Put the trailing `;` of a broken expression on its own line, aligned with the
   statement it terminates.
3. Constructor initializer and inheritance lists start on their own line,
   indented one tab, each entry led by a comma.
4. A lambda stays on one line if it fits; otherwise it expands with its brace at
   the owning statement's indent. A stored or returned lambda always expands.

### 2.4 Includes

1. Header: `#pragma once`, file comment, then includes. A `.cpp` includes its own
   header first.
2. Order: project (angle-bracketed) before standard, no blank line between;
   module-local before rest of pal; alphabetical within a group.
3. Include a header for each symbol used directly, and only those. Rely on types
   an interface you use guarantees (e.g. `uint16_t` return delivers `<cstdint>`);
   but do not lean on symbols that merely arrive transitively.

### 2.5 Naming

1. Suffix non-public data members with `_` (`head_`).
2. Prefix internal-only identifiers (macros, headers, helpers) with `__`
   (`__pal_errc`, `__diagnostic.hpp`).

### 2.6 Documentation

1. Document public API with Doxygen `///`; open each header with a `/** \file
   ... */` block, marking internal ones `\internal`. Refer to parameters with
   `\a`.
2. Do not document `__`-prefixed or internal-namespace symbols. Guard non-API
   declarations kept in a header with `/// \cond` ... `/// \endcond`.

### 2.7 Idioms

1. Every function returning `pal::result<>` is `[[nodiscard]]`; also mark queries
   and extractors whose point is the returned value (`empty`, `head`, `pop`,
   `release`). Do not blanket-mark: side-effecting returns (`push`, `remove`) and
   free pure helpers are left unmarked.
2. Mark non-throwing functions `noexcept`.
3. Prefer designated initializers for aggregates (`{.ptr = out, .ec = {}}`).
4. Confine anonymous-namespace helpers to the `.cpp`; wrap tool suppressions in
   matching markers (`// NOLINTBEGIN`/`END`, `// LCOV_EXCL_START`/`STOP`).
5. Preserve the row structure of hand-laid data tables; do not repack them.
