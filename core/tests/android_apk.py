from pathlib import Path
import re
import sys
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
APK_PATHS = []


def native_libraries(archive):
    libraries = {}
    for entry in archive.namelist():
        parts = entry.split("/")
        if len(parts) == 3 and parts[0] == "lib" and parts[2].endswith(".so"):
            libraries.setdefault(parts[1], set()).add(parts[2])
    return libraries


class AndroidApkChecks(unittest.TestCase):
    def test_all_native_libraries_are_extractable_in_release(self):
        for path in APK_PATHS:
            with self.subTest(apk=str(path)), zipfile.ZipFile(path) as archive:
                libraries = native_libraries(archive)
                self.assertTrue(libraries, "No packaged native libraries")
                for abi, names in libraries.items():
                    with self.subTest(abi=abi):
                        invalid = sorted(name for name in names if not name.startswith("lib"))
                        self.assertEqual(invalid, [], "Non-debuggable installs will skip these libraries")
                        for name in names:
                            with archive.open(f"lib/{abi}/{name}") as library:
                                self.assertEqual(library.read(4), b"\x7fELF", name)

    def test_all_default_modules_are_packaged_for_every_abi(self):
        source = (ROOT / "core/src/core.cpp").read_text(encoding="utf-8")
        expected = set(re.findall(r'conf\["modules"\]\[modCount\+\+\]\s*=\s*"([^"]+)"', source))
        self.assertTrue(expected)
        expected.add("libsdrpp_core.so")
        for path in APK_PATHS:
            with self.subTest(apk=str(path)), zipfile.ZipFile(path) as archive:
                libraries = native_libraries(archive)
                self.assertTrue(libraries)
                for abi, names in libraries.items():
                    with self.subTest(abi=abi):
                        self.assertEqual(sorted(expected - names), [], "Missing runtime modules")

    def test_library_inventory_matches_across_abis_and_variants(self):
        expected = None
        for path in APK_PATHS:
            with zipfile.ZipFile(path) as archive:
                for abi, names in native_libraries(archive).items():
                    with self.subTest(apk=str(path), abi=abi):
                        if expected is None:
                            expected = names
                        self.assertEqual(names, expected)
        self.assertIsNotNone(expected)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Usage: python core/tests/android_apk.py <apk> [<apk> ...]")
    APK_PATHS = [Path(argument) for argument in sys.argv[1:]]
    unittest.main(argv=[sys.argv[0]])
