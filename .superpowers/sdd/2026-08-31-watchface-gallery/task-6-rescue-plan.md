# Task 6 Rescue Plan

The rescue covers the shared face helpers, ABI/package tooling, and the
first-party build driver required before Tasks 7–9 can add renderer projects.
The Tasks 7–9 plan-authored stable identifiers (`watchy.firstparty.grid01`,
`grid02`, `grid03`, `term01`, `term02`, `term03`, `slab`, and `orbit`) remain
the manifest IDs; only project directories and WPK output filenames use the
hyphenated slugs (`grid-01`, etc.).

## TDD work units

1. Add Python compatibility and builder-fixture tests for ABI 1.0/1.1/1.2,
   exact capabilities, CLI selection, build/clean/audit/build/verify order,
   deterministic ELF discovery, size/runtime/architecture failures, and
   atomic promotion rollback. Run focused tests red.
2. Add C++ helper tests for every calendar/offset/leap/month/weekday boundary,
   invalid-output preservation, moon epoch/pre/post vectors and mutation;
   add descriptor ABI prefix/offset/layout and lifecycle transition tests.
   Run the face target red.
3. Implement the minimal package-tool ABI floor/ceiling and the serialized
   builder with project-native clean commands, deterministic ELF discovery,
   friendly CLI errors, and complete-set atomic promotion. Run focused tests
   green and commit.
4. Implement allocation-free face helpers and descriptor state cleanup,
   verify host CTest and forbidden-symbol/link checks, then commit.
5. Run pinned Python, full host CTest, CLI/fake-builder mutation tests and
   diff checks; move the acceptance report into the SDD directory and remove
   the root stray report. Record all evidence and remaining renderer scope.
