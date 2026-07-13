# AGENTS.md

See README.md for project overview and directory structure.

## Build & Test

- Run `ninja help` to discover available targets
- During the edit/build/test loop, use `ninja --quiet smoke` for fast feedback
- Only run `ninja all tidy sanitize format` once smoke is passing
- `ninja <target>` is the single entry point for builds and tests; invoke it as-is, never bypass it by running its steps directly. If an `<target>` breaks during other work, stop and notify the developer. Modify a target only when fixing it is the task.
