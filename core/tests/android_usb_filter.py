from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
ANDROID = "{http://schemas.android.com/apk/res/android}"


class UsbFilterChecks(unittest.TestCase):
    def test_filter_matches_native_devices(self):
        backend = (ROOT / "core/backends/android/backend.cpp").read_text(encoding="utf-8")
        tables = re.findall(r"const std::vector<DevVIDPID>\s+\w+_VIDPIDS\s*=\s*\{(.*?)\};", backend, re.S)
        expected = {
            (int(vendor, 16), int(product, 16))
            for table in tables
            for vendor, product in re.findall(r"\{\s*0x([\da-fA-F]+),\s*0x([\da-fA-F]+)\s*\}", table)
        }
        entries = ET.parse(ROOT / "android/app/src/main/res/xml/usb_device_filter.xml").getroot()
        actual = [(int(device.attrib["vendor-id"]), int(device.attrib["product-id"])) for device in entries]
        self.assertTrue(expected)
        self.assertEqual(len(actual), len(set(actual)))
        self.assertEqual(set(actual), expected)

    def test_attachment_launch_reuses_activity(self):
        manifest = ET.parse(ROOT / "android/app/src/main/AndroidManifest.xml").getroot()
        activity = manifest.find("application/activity")
        self.assertEqual(activity.get(ANDROID + "launchMode"), "singleTask")
        self.assertEqual(activity.get(ANDROID + "exported"), "true")
        attachment = "android.hardware.usb.action.USB_DEVICE_ATTACHED"
        self.assertIn(attachment, [action.get(ANDROID + "name") for action in activity.findall("intent-filter/action")])
        metadata = {entry.get(ANDROID + "name"): entry.get(ANDROID + "resource") for entry in activity.findall("meta-data")}
        self.assertEqual(metadata[attachment], "@xml/usb_device_filter")


if __name__ == "__main__":
    unittest.main()
