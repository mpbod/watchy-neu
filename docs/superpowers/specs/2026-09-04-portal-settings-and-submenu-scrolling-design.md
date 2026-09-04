# Portal Settings and Submenu Scrolling Design

## Goal

Replace the minimal package portal with a responsive, one-bit management UI based on the supplied Halftone Watch Portal reference. Let the user configure every real firmware setting, save Wi-Fi for NTP, set a home timezone and local date/time, and navigate long on-watch submenus with continuous row-by-row scrolling.

The HTML handoff is a visual and interaction reference, not executable production code and not an authority for product scope. Mock-only Bluetooth pairing, notification, backlight, automatic face sync, and firmware OTA controls remain excluded because those capabilities are deferred in firmware v1.

## Existing Constraints

- The ESP32-PICO-D4 has 4 MiB flash and no firmware OTA partition.
- The portal is served directly by ESP-IDF `esp_http_server` and must work when the browser is connected only to the temporary Watchy AP.
- The browser cannot depend on a CDN, remote font, JavaScript framework, or Internet connection.
- Wi-Fi and other radios remain off except during explicit operations.
- Portal routes continue to require HTTP Basic authentication with username `watchy` and the 32-character per-session credential shown only on the watch.
- Installed packages are trusted native code, but package input and displayed package metadata still require validation and output escaping.
- The user requested that the complete host suite run only for milestone builds. Development uses focused tests; the complete 14-suite host gate and target build run once after the milestone is integrated and reviewed.

## Chosen Architecture

The firmware retains one canonical `watchy_settings_t` model and NVS writer. The watch UI and portal both modify candidates through shared validation helpers, so the two surfaces cannot acquire different ranges or persistence semantics.

The portal page moves out of C string literals into `components/watchy_shell/web/portal.html` and is embedded as a text resource by the component build. `portal_idf.c` remains responsible for authentication, bounded request handling, hardware/service calls, and responses. Host-testable routing, validation, timezone conversion, and JSON-safe presentation policies remain outside ESP-IDF-only code.

The page uses the reference's black, white, and warm-grey palette, square controls, hairline dividers, inverted selected states, Helvetica-compatible sans typography, and local system monospace fallbacks. It has no rounded application cards, shadows, gradients, or colour status signals. It is responsive: a 248 px rail at desktop widths becomes a horizontally scrollable top navigation below 900 px, and card/spec grids collapse to one column below 560 px.

The real navigation is:

1. **Faces** — installed watchfaces, active/pending/quarantined state, WPK upload, activation, and removal.
2. **Apps** — installed app packages, state, WPK upload, and removal.
3. **Device** — live battery/storage/network status, clock controls, display/input settings, Wi-Fi, NTP, firmware/build facts, and read-only diagnostic availability.

There is no Updates route in v1. The portal never presents a simulated feature as functional.

## Settings Model

The following real values appear in the Device settings page and remain persisted through `watchy_settings_save`:

| Setting | Portal control | On-watch control | Validation |
| --- | --- | --- | --- |
| Home timezone | UTC-offset select | Scrollable UTC-offset submenu | One of the supported fixed offsets from UTC-12:00 through UTC+14:00 |
| Clock format | Square 12/24-hour switch | Existing Clock row | Boolean |
| Local date/time | Date and time inputs | Existing five-field Set Time editor | Valid Gregorian date, 2000-01-01 through 2099-12-31, hour 0-23, minute 0-59 |
| Motion wake | Square switch | Existing Motion Wake row | Boolean |
| Display motion | Full/Reduced/Off segmented control | Existing cycling row | Existing enum values only |
| Partial refresh limit | Numeric/select control | Existing Refresh cycling row | 1 through 100; watch continues stepping by five |
| NTP server | Text input | Read-only summary plus NTP Sync action | Existing hostname validation and 63-byte maximum |
| Saved Wi-Fi | SSID and write-only password fields | Existing Wi-Fi status page | Existing SSID/PSK validation |
| Active watchface | Faces action | Watchface selector | Existing package activation/promotion rules |

The home timezone is a fixed UTC offset rather than an IANA region database. That matches the capabilities of the existing POSIX timezone storage without adding a large timezone database or pretending DST rules can remain current indefinitely. Both surfaces use this exact ordered list of offsets in minutes:

`-720, -660, -600, -570, -540, -480, -420, -360, -300, -240, -210, -180, -120, -60, 0, 60, 120, 180, 210, 240, 270, 300, 330, 345, 360, 390, 420, 480, 525, 540, 570, 600, 630, 660, 720, 765, 780, 840`.

This covers the fixed civil offsets in active use, including half-hour and quarter-hour offsets. Labels use `UTC`, `UTC+07:00`, and `UTC-03:30`; the shared setter converts them to the POSIX string stored in the existing `timezone` field. Existing valid custom POSIX strings remain loadable. If one cannot be mapped to a fixed offset, the portal reports it as `CUSTOM` until the user selects a fixed home offset.

Changing the timezone changes future NTP-to-RTC conversion immediately after settings reload. It does not silently rewrite the current RTC value. The user may then set local date/time manually or run NTP Sync.

## Portal API

Existing package/status routes stay compatible. New or extended authenticated routes are:

- `GET /api/v1/settings` returns all non-secret settings plus current RTC date/time. It returns the saved SSID and `configured`, never the saved password or any password-derived value.
- `PUT /api/v1/settings` accepts the non-secret settings document. The handler loads the current settings, applies provided fields to a candidate, validates the complete candidate, and performs one NVS commit. A rejected request leaves NVS and the in-memory settings unchanged.
- `PUT /api/v1/wifi` accepts a bounded JSON object containing `ssid` and `password`. Password omission is invalid when changing the SSID; an explicit empty string selects an open network. The legacy authenticated `POST /api/v1/wifi` header form remains accepted for compatibility during v1.
- `PUT /api/v1/time` accepts local `year`, `month`, `day`, `hour`, and `minute`, validates the complete calendar value, sets seconds to zero, and writes the PCF8563 RTC. It does not mutate timezone.
- `POST /api/v1/time/ntp` synchronizes through the already-connected saved client Wi-Fi session. In AP mode it returns `409 client_mode_required`; the UI explains that the user must save Wi-Fi, leave the AP session, and reopen Portal using Saved Wi-Fi.

Request bodies are JSON, capped at 1 KiB, require `application/json`, and reject malformed JSON, duplicate logical values, wrong types, unknown enum strings, overlong strings, and out-of-range numbers. Errors use the existing public JSON error shape. State-changing responses return the newly effective non-secret values so the page can reconcile without guessing.

The browser builds all package-derived content with DOM `textContent`. It never injects package names, versions, SSIDs, server names, or error strings through `innerHTML`.

## Wi-Fi and NTP Ownership

Wi-Fi provisioning writes the same NVS namespace and keys consumed by the normal NTP path. A successful portal save is therefore the only credential source needed for later on-watch NTP Sync.

The existing NTP implementation is extracted from `main.c` into a small time service with two ownership layers:

- the synchronization primitive assumes station networking is already connected, runs SNTP with the configured server, converts UTC to the configured home timezone, writes the RTC, and tears down only SNTP;
- the on-watch action starts saved station Wi-Fi, calls the primitive, then stops Wi-Fi;
- the client-mode portal calls the same primitive without restarting or stopping the portal's station connection.

AP mode does not switch radios underneath the browser session. This avoids disconnecting the user during a request and avoids introducing AP+STA coexistence in this milestone.

## Portal Session Security and Lifetime

The temporary AP password changes from 16 to exactly 8 characters. It is generated from guaranteed hardware entropy using the existing unambiguous alphabet (no `0/O`, `1/I/l`) and is regenerated for every AP session. Eight characters provide an ephemeral local-network credential while remaining practical to type from the watch screen.

The 32-character portal authentication credential is unchanged and independent of the AP password. It remains displayed out of band on the watch and is never included in HTML, JavaScript, URLs, logs, or JSON.

Authenticated activity refreshes the inactivity clock. The portal stops after exactly five minutes without an authenticated request. The page performs one initial load and refreshes after user actions; it does not run background polling that would manufacture activity. Unauthenticated traffic never extends the session. The existing 30-minute absolute lifetime remains, so repeated user activity cannot keep a portal alive indefinitely. Back stops the server immediately and turns Wi-Fi off.

## Captive Portal Discovery

AP mode starts a bounded DNS responder on UDP port 53 after the SoftAP has acquired `192.168.4.1`. Valid single-question IPv4 hostname lookups receive `192.168.4.1`; malformed packets, unsupported opcodes, multi-question packets, and non-A queries receive no fabricated record. DNS parsing uses the received datagram length for every label and pointer check, and response construction never exceeds the request buffer. The responder has a fixed task stack and socket receive timeout so `watchy_portal_stop` can join it promptly. It never runs in saved-client mode.

The HTTP server recognizes common captive-network probes, including Android/ChromeOS `generate_204` paths, Apple `hotspot-detect.html`, and Microsoft `connecttest.txt`, `ncsi.txt`, and redirect paths. Probe requests and unmatched GET paths receive a temporary redirect to `http://192.168.4.1/` without including the session credential. The portal root then issues the normal HTTP Basic challenge, allowing the user to enter username `watchy` and the credential displayed on the watch. API routes never fall through to the captive redirect and retain their authenticated JSON errors.

The AP advertises the captive portal URL through the DHCP captive-portal option when supported by the ESP-IDF network interface. Wildcard DNS and probe redirects remain the compatibility path. HTTPS interception is intentionally unsupported because the watch has no trusted hostname certificate; `http://192.168.4.1/` remains the documented fallback when an operating system suppresses its captive-network window.

## On-Watch Home Timezone

Settings gains a **Home Zone** row adjacent to Clock and Set Time. Selecting it opens `WATCHY_SHELL_TIMEZONE`, a submenu listing human-readable fixed UTC offsets. The submenu opens with the current fixed offset selected; an existing custom POSIX zone opens on UTC without changing the stored zone until the user confirms.

- Up and Down move one offset at a time.
- Menu commits the highlighted offset, saves settings, and returns to the same Settings row.
- Back cancels without changing settings and returns to the same Settings row.
- The selected row metadata shows `UTC`, `UTC+07:00`, or the applicable offset.

The manual date/time editor continues to use local watch time. Its header/summary also shows the active home-zone label so the relationship is explicit.

## Continuous Submenu Scrolling

`watchy_shell_t` gains a viewport start and a saved parent cursor/viewport for the current submenu depth. Long lists no longer derive their first visible item from fixed three-row pages.

For a three-row viewport:

- moving within the visible window changes only the selected row;
- moving Down past the bottom shifts the viewport down by one item;
- moving Up past the top shifts the viewport up by one item;
- wrapping from last to first restores viewport start zero;
- wrapping from first to last shows the final three items;
- entering a child initializes its cursor deliberately (zero, active face, current timezone, or current editor field as appropriate);
- Back restores the exact parent screen, selected row, and viewport start.

This applies to Settings, Home Zone, Watchfaces, Apps, Diagnostics, and any other list exceeding three rows. Launcher and the two-item Portal mode screen do not scroll.

Scrolling uses the existing single-write attended Cut presentation so each button press remains responsive. It does not use a multi-write visual Push for every row movement. The right-side rail remains proportional to absolute selection and list length, providing position feedback while content moves continuously.

## Error Handling and Recovery

- Settings validation failure returns a field-neutral `invalid_settings` response and preserves prior settings.
- Wi-Fi validation failure returns `invalid_wifi`; the password buffer is zeroed on every exit path.
- RTC validation or write failure returns `invalid_time` or `rtc_error` and does not alter persisted settings.
- NTP is rejected if no Wi-Fi is saved or the portal is not in client mode. Timeout/failure leaves the prior RTC value intact.
- Portal API failures are displayed in a persistent result region and do not optimistically toggle controls.
- If settings changed during a portal session, normal portal exit reloads settings, reapplies the display partial limit and motion policy, reconciles the active face, and forces the established safe watchface return path.
- A malformed embedded page or response buffer failure fails closed with the existing 500 response; it never exposes credentials.

## Testing

Focused host tests are written before each production change and must first fail for the intended missing behavior.

Host coverage includes:

- 8-character AP password length, alphabet, entropy failure, and per-session generation contract;
- inactivity just before/at five minutes, timestamp wraparound, authenticated-only extension, and unchanged absolute timeout;
- routes and methods for settings, Wi-Fi, manual time, and NTP;
- timezone offset label/POSIX conversion, custom legacy zones, supported half/quarter offsets, and range rejection;
- candidate settings patching, complete validation, password secrecy, and JSON escaping;
- Gregorian date validation including leap years and the 2000-2099 boundary;
- continuous viewport movement, both wraps, short lists, parent cursor restoration, active timezone initialization, and every affected list type;
- portal HTML structural/style contract, responsive breakpoints, no remote assets, no mock-only routes, no secret interpolation, and safe DOM text insertion;
- captive DNS response bounds, A-record address, malformed and unsupported query rejection, AP-only lifecycle, probe redirects, and API non-redirection;
- NTP ownership policy for on-watch, client-portal, AP-portal, and missing-credential paths.

After focused tests pass, a read-only code review checks security, input bounds, radio ownership, NVS behavior, display responsiveness, and regression risk. The milestone gate then runs all 14 host suites, portal static checks, the ESP32 `watchy_v2` target build, `git diff --check`, and verifies the exact commit embedded in the ELF.

Hardware UAT verifies:

1. Settings starts the AP and shows an 8-character password plus the unchanged authentication credential.
2. Connecting a phone, tablet, macOS, Windows, Android, or iOS device to the AP opens or flags the captive portal; `http://192.168.4.1/` works as the fallback.
3. The page matches the monochrome responsive reference on phone and desktop.
4. Saving SSID/password survives portal exit and reset; the password is never returned by the API.
5. Reopening Portal through Saved Wi-Fi permits NTP Sync and updates the RTC using the selected home timezone.
6. Portal and watch can set timezone plus local date/time, and Hairline/Grid faces display the resulting local time.
7. Portal stops after five inactive minutes, after Back, and at the 30-minute absolute cap.
8. Every long on-watch list moves one row at a time, wraps correctly, and restores its parent cursor on Back.
9. Package upload, activation, rollback, removal, safe mode, and full-refresh face return continue to work.

After the milestone gate passes, the reviewed firmware is flashed over serial without erasing NVS or LittleFS, preserving the user's packages and settings for UAT.
