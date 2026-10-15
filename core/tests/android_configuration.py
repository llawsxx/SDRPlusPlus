from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
ANDROID = "{http://schemas.android.com/apk/res/android}"


class AndroidConfigurationChecks(unittest.TestCase):
    def setUp(self):
        self.manifest = ET.parse(ROOT / "android/app/src/main/AndroidManifest.xml").getroot()

    def test_target_sdk_matches_compile_sdk(self):
        gradle = (ROOT / "android/app/build.gradle").read_text(encoding="utf-8")
        compile_sdk = int(re.search(r"compileSdk\s*=\s*(\d+)", gradle).group(1))
        target_sdk = int(re.search(r"targetSdkVersion\s+(\d+)", gradle).group(1))
        minimum_sdk = int(re.search(r"minSdkVersion\s+(\d+)", gradle).group(1))
        self.assertEqual(target_sdk, compile_sdk)
        self.assertGreaterEqual(target_sdk, 36)
        self.assertEqual(minimum_sdk, 28)
        self.assertNotIn("ExpiredTargetSdkVersion", gradle)
        self.assertNotRegex(gradle, r"checkReleaseBuilds\s*(?:=\s*)?false")
        self.assertNotRegex(gradle, r"abortOnError\s*(?:=\s*)?false")

    def test_legacy_storage_permissions_are_capped(self):
        permissions = {
            entry.get(ANDROID + "name"): entry
            for entry in self.manifest.findall("uses-permission")
        }
        self.assertEqual(
            permissions["android.permission.WRITE_EXTERNAL_STORAGE"].get(ANDROID + "maxSdkVersion"), "28"
        )
        self.assertEqual(
            permissions["android.permission.READ_EXTERNAL_STORAGE"].get(ANDROID + "maxSdkVersion"), "32"
        )
        self.assertIn("android.permission.INTERNET", permissions)

    def test_lock_back_handler_and_orientation_are_preserved(self):
        activity = self.manifest.find("application/activity")
        self.assertEqual(activity.get(ANDROID + "enableOnBackInvokedCallback"), "false")
        properties = {
            entry.get(ANDROID + "name"): entry.get(ANDROID + "value")
            for entry in activity.findall("property")
        }
        self.assertEqual(
            properties["android.window.PROPERTY_COMPAT_ALLOW_RESTRICTED_RESIZABILITY"], "true"
        )


if __name__ == "__main__":
    unittest.main()
