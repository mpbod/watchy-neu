import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PORTAL = ROOT / "components/watchy_shell/web/portal.html"


class PortalWebTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.page = PORTAL.read_text(encoding="utf-8")

    def test_portal_is_offline_responsive_and_has_only_real_routes(self) -> None:
        page = self.page
        self.assertIn("--rail-width:248px", page)
        self.assertIn("@media (max-width:899px)", page)
        self.assertIn("@media (max-width:559px)", page)
        self.assertNotRegex(page, r"https?://(?!192\.168\.4\.1)")
        self.assertNotIn("Firmware update", page)
        self.assertNotIn("Backlight on shake", page)
        for label in (
            "Faces",
            "Apps",
            "Device",
            "Home timezone",
            "NTP server",
            "Wi-Fi network",
            "Date",
            "Time",
            "Motion wake",
            "Display motion",
            "Partial refresh limit",
        ):
            self.assertIn(label, page)

    def test_untrusted_values_use_text_content(self) -> None:
        self.assertNotIn("innerHTML", self.page)
        self.assertIn("textContent", self.page)
        self.assertNotIn("X-Watchy-Token", self.page)

    def test_page_uses_the_documented_api_contract_without_polling(self) -> None:
        for route in (
            "/api/v1/status",
            "/api/v1/packages",
            "/api/v1/settings",
            "/api/v1/wifi",
            "/api/v1/time",
            "/api/v1/time/ntp",
        ):
            self.assertIn(route, self.page)
        self.assertIn("application/octet-stream", self.page)
        self.assertNotIn("setInterval", self.page)

    def test_review_regressions_keep_controls_safe_and_live(self) -> None:
        page = self.page
        self.assertIn('id="removeConfirm"', page)
        self.assertIn('id="batteryFill"', page)
        self.assertIn('id="wifiHint"', page)
        self.assertIn('state.passwordForSsid', page)
        self.assertNotIn("#17171a", page)
        self.assertIn("const TIMEZONE_OFFSETS = Object.freeze(", page)

    def test_toggle_group_labels_have_an_explicit_in_cell_layout(self) -> None:
        self.assertIn(".setting .setting-legend { display:block;", self.page)
        self.assertIn('class="setting toggle-group" role="group"', self.page)

    def test_embedded_page_replaces_c_string_fragments(self) -> None:
        cmake = (ROOT / "components/watchy_shell/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        portal_c = (ROOT / "components/watchy_shell/src/portal_idf.c").read_text(
            encoding="utf-8"
        )
        self.assertIn('EMBED_TXTFILES "web/portal.html"', cmake)
        self.assertNotIn("PAGE_HEAD", portal_c)
        self.assertNotIn("PAGE_SCRIPT", portal_c)
        self.assertIn("_binary_web_portal_html_start", portal_c)
        self.assertIn("_binary_web_portal_html_end", portal_c)
        self.assertRegex(portal_c, re.compile(r"httpd_resp_send\(request,.*portal_html_start", re.S))


if __name__ == "__main__":
    unittest.main()
