# Task 6 acceptance report

## RED

Before implementation, `python3 -m unittest tests.python.test_first_party -v` failed with `ModuleNotFoundError: tools.build_first_party`. The requested host target was also absent. This established that the tests exercised new behavior.

## GREEN

- The first-party Python matrix test passes: eight unique, sorted exact slugs/output names, ABI 1.2, watchface type, and expected capability masks.
- The host face test passes with pinned Ninja. It covers checked date validation, leap-month boundaries, UTC offset range and cross-day rollover, weekday recomputation, the approved lunar epoch (2000-01-06 18:14 UTC = octant 0), and descriptor lifecycle/context retention.
- `build_first_party.py --list` and `--dry-run` work when invoked directly by path. Real eight-package builds are intentionally not claimed until renderer Tasks 7–9 provide projects.
- The full host CTest suite passes with pinned CTest (13/13).

The full Python suite still has two unrelated environment/integration issues: `fontTools` is not installed in the active interpreter, and the existing motion settings-label assertion expects an older source string. No first-party test depends on either condition.
