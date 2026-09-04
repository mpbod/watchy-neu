import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ELF_LOADER_HASH = "95d0cc617069a3f0b935d9b20bdc6065e6be8fc65fb63215fd492f0ac0588954"
IDF_IMAGE_DIGEST = "00e94c6ff8bc1f7bd22b234ff43db3f3056cb33dedac6819df9fbedbcb5c6ebb"


class ReproducibilityContracts(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_firmware_template_and_samples_pin_exact_loader(self) -> None:
        firmware = self.read("components/watchy_packages/idf_component.yml")
        self.assertIn('version: "==5.5.0"', firmware)
        self.assertIn('version: "1.3.3"', firmware)
        self.assertNotRegex(firmware, r"1\.3\.(?:\*|x)")
        for relative in (
            "sdk/package-template/main/idf_component.yml",
            "samples/digital-watchface/main/idf_component.yml",
            "samples/hardware-demo/main/idf_component.yml",
        ):
            manifest = self.read(relative)
            self.assertIn('espressif/elf_loader: "1.3.3"', manifest, relative)
            self.assertNotRegex(manifest, r"1\.3\.(?:\*|x)", relative)

    def test_ci_uses_the_verified_immutable_idf_image(self) -> None:
        workflow = self.read(".github/workflows/ci.yml")
        self.assertIn(f"espressif/idf@sha256:{IDF_IMAGE_DIGEST}", workflow)
        self.assertNotIn("espressif/idf:v5.5", workflow)

    def test_dependency_locks_capture_idf_and_loader_artifact(self) -> None:
        for relative in (
            "dependencies.lock",
            "samples/digital-watchface/dependencies.lock",
            "samples/hardware-demo/dependencies.lock",
        ):
            lock = self.read(relative)
            self.assertRegex(lock, r"(?m)^    version: 1\.3\.3$")
            self.assertIn(f"component_hash: {ELF_LOADER_HASH}", lock)
            self.assertRegex(lock, r"(?m)^    version: 5\.5\.0$")

    def test_portal_page_never_contains_the_session_credential(self) -> None:
        portal = self.read("components/watchy_shell/src/portal_idf.c")
        self.assertNotIn("X-Watchy-Token", portal)
        self.assertNotRegex(portal, r"httpd_resp_send_chunk\([^\n]*info\.token")
        self.assertNotRegex(portal, r"const TOKEN")
        self.assertIn('read_header(request, "Authorization"', portal)
        self.assertIn("watchy_portal_basic_authorized", portal)
        upload = portal[portal.index("static esp_err_t receive_upload"):
                        portal.index("static esp_err_t mutate_package")]
        self.assertIn("watchy_portal_timed_out()", upload)

    def test_portal_ap_uses_the_shared_eight_character_password_mapper(self) -> None:
        header = self.read("components/watchy_shell/include/watchy/portal.h")
        policy = self.read("components/watchy_shell/src/portal_policy.c")
        portal = self.read("components/watchy_shell/src/portal_idf.c")
        self.assertIn("#define WATCHY_PORTAL_AP_PASSWORD_SIZE 8u", header)
        self.assertIn("watchy_portal_password_from_digest", policy)

        derive_start = portal.index("static bool derive_ap_password")
        network_start = portal.index("static watchy_status_t start_network", derive_start)
        derive = portal[derive_start:network_start]
        self.assertIn("watchy_portal_password_from_digest", derive)
        self.assertNotIn("out_size < 17u", derive)
        self.assertNotIn("index < 16u", derive)
        self.assertNotIn("out_password[16]", derive)

        network_end = portal.index("watchy_status_t watchy_portal_start", network_start)
        network = portal[network_start:network_end]
        self.assertIn("derive_ap_password(config.password, sizeof(config.password))", network)
        self.assertIn("watchy_wifi_start_ap(&config)", network)

    def test_settings_labels_distinguish_motion_wake_from_display_effects(self) -> None:
        rendering = self.read("components/watchy_shell/src/shell_render.c")
        # The approved gallery plan names the rows "Motion Wake" and
        # "Display Motion".  This source now uses the row labels directly;
        # the former all-caps metadata strings belonged to the superseded
        # renderer contract.
        self.assertIn('"Motion Wake"', rendering)
        self.assertIn('"Display Motion"', rendering)
        self.assertNotIn('"MOTION %s"', rendering)

    def test_runner_stop_propagates_callback_cleanup_failure(self) -> None:
        runtime = self.read("components/watchy_packages/src/idf_runtime.c")
        finish_start = runtime.index("static watchy_package_status_t runner_finish")
        post_start = runtime.index("static watchy_package_status_t runner_post_callback")
        finish = runtime[finish_start:post_start]
        self.assertIn("watchy_package_finalize_watchface_attempt(", finish)
        self.assertIn("lifecycle_status, stop_status", finish)
        self.assertIn("return result;", finish)

        stop_start = runtime.index("watchy_package_status_t watchy_packages_runner_stop")
        stop_end = runtime.index("bool watchy_packages_runner_active", stop_start)
        stop = runtime[stop_start:stop_end]
        self.assertIn("status = runner_finish(WATCHY_PACKAGE_OK);", stop)
        self.assertIn("return status;", stop)

    def test_runner_watchdog_failure_closes_the_active_session(self) -> None:
        runtime = self.read("components/watchy_packages/src/idf_runtime.c")
        boundaries = (
            ("watchy_package_status_t watchy_packages_runner_event",
             "static watchy_status_t runner_display_present"),
            ("watchy_package_status_t watchy_packages_runner_render",
             "watchy_package_status_t watchy_packages_runner_stop"),
            ("watchy_package_status_t watchy_packages_runner_stop",
             "bool watchy_packages_runner_active"),
        )
        for start_marker, end_marker in boundaries:
            start = runtime.index(start_marker)
            body = runtime[start:runtime.index(end_marker, start)]
            watchdog_failure = body[
                body.index("watchy_watchdog_scope_begin"):
                body.index("watchy_package_session_", body.index("watchy_watchdog_scope_begin"))
                if "watchy_package_session_" in body[body.index("watchy_watchdog_scope_begin"):]
                else len(body)
            ]
            self.assertIn("runner_finish(WATCHY_PACKAGE_ERR_STATE);",
                          watchdog_failure, start_marker)
            self.assertNotIn("runner_finish(false);", watchdog_failure, start_marker)


if __name__ == "__main__":
    unittest.main()
