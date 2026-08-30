# Motion final-review fix report

## Result

- Fix base: `ff25479ad17db570e95b74a52f72ab9bd2200d0b`
- Implementation commit: `9ad3c28a564acf118e632bc82738ca22ba97fbf6` (`fix: close motion final-review gaps`)
- Automated status: complete and green.
- Physical-device UAT: not performed. No physical acceptance item or checkbox was changed.

## Numbered findings

### 1. Watchdog enrollment lifetime

Implementation:

- Added `watchy_watchdog_scope_t` in `components/watchy_core/include/watchy/watchdog.h` and `components/watchy_core/src/watchdog.c`. A scope records whether it enrolled the current task, feeds only while active, and removes only an enrollment it owns.
- `watchy_display_present()` now owns a scope around the complete presentation and ends it after every execution result. A nested display presentation inside a package callback sees the package scope's enrollment and does not remove it.
- Package runner start/event/render/stop operations now own scopes and release the scope and package mutex on every scoped exit. Watchdog-admission failure on an already-active runner also closes the package session, preserving the prior cleanup/failure contract.
- The idle shell wait is not enrolled accidentally after either presentation site returns.

Exact coverage:

- `test_nested_display_and_package_watchdog_scopes_leave_idle_shell_unenrolled` in `tests/host/test_core.c`: nested package/display ownership, pre-enrolled caller preservation, idle-shell unenrollment, enrollment failure, initial-feed failure cleanup, and unenrollment failure reporting.
- `test_runner_watchdog_failure_closes_the_active_session` in `tests/python/test_reproducibility.py`: event/render/stop watchdog-failure branches close the active runner before returning.
- `test_runner_stop_propagates_callback_cleanup_failure` in the same Python suite: stop callback status still survives scope/mutex cleanup.
- Final `platformio run -e watchy_v2`: both ESP-IDF watchdog adapters and all package/display call sites compiled and linked.

### 2. Plan-wide mandatory-Clear forecasting

Implementation:

- Added `watchy_display_plan_requires_clear()`, which simulates the complete physical-write sequence against the retained partial counter before the first optional write.
- `watchy_display_execute_plan()` first injects the actual requested target mode, resolves conditional Fill length, and forecasts the resulting complete plan. Any optional sequence that would reach or cross the partial limit is replaced by the direct mandatory two-full-write Clear plan.
- Per-write promotion remains in place as a fail-safe, while retained counters still commit only after each successful physical write.

Exact coverage:

- `test_display_forecasts_every_optional_write_and_actual_target_mode` in `tests/host/test_hal.c`: write-count classes 1 through 5, partial target, and full target.
- `test_display_transition_adapter_clears_before_plan_wide_threshold`: proves no optional physical write occurs before the direct `[FULL, FULL]` Clear and validates result/write/retained accounting.
- Existing `test_display_refresh_policy_promotes_and_counts_each_physical_write` and `test_transition_mandatory_clear_inverts_then_targets` continue to cover physical accounting and the truthful two-write Clear frames.

Mutation check:

- Temporarily bypassing the new plan forecast made `watchy_hal_tests` fail at the expected two-write assertion in `test_display_transition_adapter_clears_before_plan_wide_threshold`; restoring the forecast returned `PASS 21 HAL tests`.

### 3. Cancellation propagation

Implementation:

- Appended stable `WATCHY_STATUS_CANCELLED = -6` and added pure execution-status/button-edge helpers.
- The HAL now latches the exact newly sampled four-button mask, returns a distinct cancelled status, leaves the successfully written intermediate frame as retained truth, and forces the next accepted presentation to Cut.
- Shell presentation state queues cancellation input, clears a sleep request, prevents sleep while input is pending, consumes the mask once, and processes it before the next target presentation. Boot-time package cancellation skips the generic initial shell refresh so that the queued input, rather than that refresh, consumes the one-shot Cut.
- Package presentation classification distinguishes target/cancelled/failed. Only a completed target marks a render complete or promotes pending state. Cancellation is non-failure both for render presentation and for a package-requested post-callback refresh.
- Package-app and boot-watchface callers take the HAL mask before sleeping or classifying the package as failed.

Exact coverage:

- `test_transition_executor_cancels_optional_sequence_at_write_boundaries` and `test_transition_executor_completes_mandatory_clear_without_cancellation` in `tests/host/test_core.c`.
- `test_display_transition_adapter_cancellation_retains_last_successful_frame` and `test_display_cancellation_maps_to_distinct_status_and_button_edges` in `tests/host/test_hal.c`.
- `test_transition_present_consumes_once_and_only_falls_back_on_rejection`, `test_package_presentation_classification_defers_promotion_on_cancellation`, and `test_runner_post_policy_requires_cleanup_for_exit_or_refresh_failure` in `tests/host/test_packages.c`.
- `test_presentation_outcomes_gate_sleep_and_post_action_refresh` in `tests/host/test_shell.c`: queued mask, sleep gate, consume-once behavior, and pending-input gate.
- Final firmware compile covers the HAL/shell/package/main propagation path.

Additional RED/GREEN evidence:

- Adding the pending-input sequencing assertion first produced the expected undeclared-function compile failure in `watchy_shell_tests`; the policy/API and main gate then made the suite green.
- Adding the cancelled-vs-failed package post-action cases first made `watchy_packages_tests` fail on `WATCHY_PACKAGE_PRESENT_FAILED`; the outcome-aware policy then returned `PASS 45 package tests`.

### 4. Aligned WPK request copy

Implementation:

- `host_request_transition()` retains strict readable-range admission, then copies the admitted bytes with `memcpy` into an aligned local `watchy_transition_request_v1_t`. Validation and latching use only that stable copy.

Exact coverage:

- `test_transition_callback_admits_only_readable_active_requests` in `tests/host/test_packages.c` passes a deliberately unaligned but readable request, verifies the range callback saw the original address/size, and verifies the latched aligned value byte-for-byte.
- A UBSan alignment mutation temporarily restored the typed dereference. With `-fsanitize=undefined -fno-sanitize-recover=alignment`, `watchy_packages_tests` exited 134 and reported `transition_policy.c:85: member access within misaligned address ... requires 4 byte alignment`. Restoring the `memcpy` implementation produced `PASS 45 package tests` under the same UBSan build. The generated UBSan tree was then cleaned with its native CMake clean target.

### 5. Exact ABI v1.1 prefix assertion

Implementation:

- Added exact C and C++ v1.1 system prefix fixtures containing context, millis, sleep, log, request-exit, and request-refresh.
- Both languages assert `offsetof(watchy_system_api_v1_t, request_transition) == sizeof(v1_1_prefix)` and lock the appended cancelled status value.

Exact coverage:

- `watchy_sdk_c_tests` compiles the `_Static_assert` fixtures in `tests/host/test_sdk_c.c`.
- `watchy_sdk_cpp_tests` compiles the `static_assert` fixtures in `tests/host/test_sdk_cpp.cpp`.
- Both targets passed in focused and full CTest runs.

### 6. Fill for explicit sync progress

Implementation:

- Added an explicit `sync_progress` transition semantic and `watchy_shell_sync_progress_rect()` returning `{4, 75, 192, 19}`, matching the 200x200 NTP status row.
- NTP synchronization now emits a localized Fill request with `WATCHY_TRANSITION_HAS_RECT`, zero reserved fields, and full core validation before use.

Exact coverage:

- `test_shell_transition_mapping_returns_complete_safe_requests` in `tests/host/test_shell.c` asserts Fill, exact rectangle, exact flags, zero reserved fields, and `watchy_transition_validate(...) == WATCHY_STATUS_OK`.

### 7. Localized saved-state Flash

Implementation:

- Added `has_rect`/`rect` to shell transition context and `watchy_shell_settings_confirmation_rect()`, which maps the eight compact settings rows to their exact clipped highlight rectangles (`x=4`, `width=192`, `height=15`, `y=30+21*selection`).
- Saved-state Flash now requires and emits `WATCHY_TRANSITION_HAS_RECT`; a missing rectangle rejects the transition request instead of falling back to a whole-screen inversion.

Exact coverage:

- `test_shell_transition_mapping_returns_complete_safe_requests`: exact localized row-0 Flash request, row-7 geometry boundary, request validity, zero reserved fields, and missing-rectangle rejection.

### 8. Avoid redundant fifth Fill write

Implementation:

- Added `watchy_transition_resolve_plan()`. It composes the 100% Fill frame and compares it with the complete target: equality resolves to four writes; otherwise the target remains a fifth write.
- The compositor accepts only deterministic four/five Fill plans. Both the generic executor and HAL adapter resolve before execution/forecasting so cancellation boundaries, target mode, result count, feed count, and physical-write accounting use the same actual plan.

Exact coverage:

- `test_transition_fill_omits_only_a_redundant_target_write` in `tests/host/test_core.c`: equality gives four writes with the fourth target/full; non-equality gives five writes with the fifth target/full; result/feed/cancel counts are exact.
- Plan-wide HAL tests exercise the resolved write count before forecasting.

Mutation check:

- Temporarily preventing the executor from switching to the resolved plan made `watchy_core_tests` fail on the expected four-write equality count. Restoring the resolved plan returned `PASS 37 tests`.

### 9. C++ diagnostic newline

Implementation:

- Replaced the literal `\\n` in the C++ SDK `CHECK` diagnostic with an actual newline escape.

Exact coverage:

- `watchy_sdk_cpp_tests` compiled and passed in focused and full CTest runs.

### 10. ABI v1.2/null transition callback sample fixture

Implementation:

- Parameterized the digital-sample exercise for callback availability and added an ABI v1.2 host whose `request_transition` pointer is null.
- The fixture performs the qualifying button event plus minute change and verifies rendering succeeds without a request call.

Exact coverage:

- `watchy_digital_sample_tests`, specifically the `abi_v12_without_transition_callback` exercise in `tests/host/test_digital_sample.cpp`.

### 11. Distinct Settings labels

Implementation:

- Renamed the accelerometer setting to `MOTION WAKE` and the presentation setting to `DISPLAY FX`, retaining the compact-row typography and layout.

Exact coverage:

- `test_settings_labels_distinguish_motion_wake_from_display_effects` in `tests/python/test_reproducibility.py` locks both distinct labels and rejects the old ambiguous `MOTION %s` label.
- Final firmware build compiled the actual shell renderer.

## RED/GREEN summary

The work was performed test-first in subsystem waves:

1. The first RED build failed on the newly declared watchdog, forecast, cancellation-status, and button-mask contracts. Core/HAL implementation made the focused core and HAL suites green.
2. The second RED build failed on the new shell context/outcome APIs and package presentation classification. Shell/package/SDK/sample implementation made all focused targets green.
3. Regression mutations independently demonstrated that forecast bypass, unresolved Fill execution, and unaligned typed WPK access are detected by their focused tests/sanitizer.
4. The first complete Python regression exposed the pre-existing source contract's expectation that runner-stop return its cleanup status explicitly. The scope-cleanup result was assigned back to `status`, preserving both behaviors; focused and complete reruns passed.
5. Self-review RED cases caught boot cancellation ordering, active-runner cleanup on watchdog failure, and package post-refresh cancellation classification before final verification.

Focused final results:

- `watchy_core_tests`: `PASS 37 tests`
- `watchy_hal_tests`: `PASS 21 HAL tests`
- `watchy_packages_tests`: `PASS 45 package tests`
- `watchy_shell_tests`: passed
- `watchy_sdk_c_tests`: passed
- `watchy_sdk_cpp_tests`: passed
- `watchy_digital_sample_tests`: passed

## Required verification

### Full host and Python regression

Final command:

```text
export PATH="/Users/maxb/.platformio/packages/tool-cmake/bin:/Users/maxb/.platformio/packages/tool-ninja:$PATH"
cmake --build build/host && ctest --test-dir build/host --output-on-failure && python3 -m unittest discover -s tests/python -v
```

Result:

- CTest: 9/9 targets passed, 0 failed.
- Python: 21/21 tests passed.

### Two independently clean sample WPK rounds

Each round began after native PlatformIO clean targets for both samples, then ran:

```text
python3 tools/build_samples.py --output-dir build/samples-round1
python3 tools/build_samples.py --output-dir build/samples-round2
```

Both rounds reported:

- Digital ELF audit: `exports=watchy_package_entry undefined=0 symbol_relocations=0`
- Digital WPK verify: `sample.digital@1.0.0`, 9,117 bytes, embedded digest `d6d731eb0e473417faaf196312d419fd25f94805b18c2a5a4c61d794e115de98`
- Hardware ELF audit: `exports=watchy_package_entry undefined=0 symbol_relocations=0`
- Hardware WPK verify: `sample.hardware@1.0.0`, 11,994 bytes, embedded digest `0555a011994e7b25bd31816185667c71f0e6face3f78ff1ecc2945387b9d607c`

Outer-file SHA-256 values were byte-identical between rounds:

- `digital.wpk`: `22361c016307cbe8e715280a72222ec5c5e2e3b1e50a884f5e3c8dafc69ededd`
- `hardware-demo.wpk`: `07f08547dc0e0cccbd923a38b9583d7a61e411057f70e4cfa1299d1cf1093a03`

`cmp -s` confirmed both pairs byte-identical. Both generated sample trees were cleaned afterward with `platformio run -d <sample> -t clean`.

### Firmware, size, and stack

Final command: `platformio run -e watchy_v2`

Result: success in 44.82 seconds.

- RAM: 116,080 / 327,680 bytes (35.4%).
- Flash program usage: 1,339,331 / 1,835,008 bytes (73.0%).
- Flash headroom: 495,677 bytes (27.0%).
- Generated `firmware.bin`: 1,344,864 bytes.

Before native cleanup, 37 generated `.su` files were present. Relevant static-frame evidence:

- `refresh_shell`: 96 bytes
- `watchy_display_present`: 144 bytes
- `watchy_display_execute_plan`: 112 bytes
- `watchy_packages_runner_start`: 896 bytes
- `watchy_packages_runner_event`: 48 bytes
- `watchy_packages_runner_render`: 64 bytes
- `watchy_packages_runner_stop`: 48 bytes
- `host_request_transition`: 64 bytes
- shell presentation observe/take/pending-input helpers: 32 bytes each
- top-level `app_main`: 1,440 bytes

The generated firmware tree was then removed with `platformio run -e watchy_v2 -t clean`.

## Files and commit

Implementation commit:

- `9ad3c28a564acf118e632bc82738ca22ba97fbf6` — `fix: close motion final-review gaps`

The implementation commit changes 27 files (1,014 insertions, 136 deletions) across:

- core transition/watchdog implementation and headers;
- HAL display/policy implementation and headers;
- package policy/runtime/host API;
- shell policy/rendering/API and firmware main integration;
- SDK status ABI;
- core, HAL, package, shell, SDK, sample, and Python regression tests.

This report is committed separately under the subject `docs: record motion final-review verification` so the implementation SHA above remains stable and auditable.

## Complete-diff self-review

Self-review covered the complete `ff25479ad17db570e95b74a52f72ab9bd2200d0b..9ad3c28a564acf118e632bc82738ca22ba97fbf6` diff, followed by `git diff --check` with no errors.

The review explicitly rechecked these prior guarantees:

- framebuffer storage remains fixed/static;
- the kernel remains the only panel-write authority;
- ABI v1.1 layout remains compatible and is now locked by exact prefix fixtures;
- WPK pointers remain range-admitted, are now alignment-safe, and requests remain consume-once;
- retained state commits only after successful physical writes and records the actual intermediate frame on cancellation;
- the bounded existing display recovery remains a single retry;
- cleanup is ownership-aware and covers success/failure exits;
- mandatory Clear remains direct, truthful, two-write, and non-cancellable;
- no physical-UAT evidence was invented and no live acceptance box was closed.

No additional correctness issue remained after the self-review fixes and final verification.

## Remaining concerns

- Physical e-paper waveform quality, energy use, ghosting behavior, and real button timing still require device UAT. Those acceptance items intentionally remain open.
- The sample toolchain continues to print its existing shared-object RWX-segment and sample flash-size warnings; both ELF audits and both WPK verifications pass, and these warnings are outside the motion final-review findings.
