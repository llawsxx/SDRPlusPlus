import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCES = json.loads((ROOT / "sources.json").read_text())
LIBRARIES = {
    "zstd": ("libzstd_shared", "zstd"),
    "fftw": ("fftw3f", "fftw3f"),
    "volk": ("volk", "volk"),
    "libusb": ("usb1.0", "usb1.0"),
    "airspy": ("airspy", "airspy"),
    "airspyhf": ("airspyhf", "airspyhf"),
    "hydrasdr": ("hydrasdr", "hydrasdr"),
    "hackrf": ("hackrf", "hackrf"),
    "rtlsdr": ("rtlsdr", "rtlsdr"),
    "codec2": ("codec2", "codec2"),
    "libxml2": ("LibXml2", "xml2"),
    "libiio": ("iio", "iio"),
    "libad9361": ("ad9361", "ad9361"),
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fetch(name, cache):
    metadata = SOURCES[name]
    archive = cache / "downloads" / (metadata["sha256"] + ".tar")
    destination = cache / "sources" / (name + "-" + metadata["sha256"])
    marker = destination / ".complete"
    if marker.exists():
        return destination
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists() or digest(archive) != metadata["sha256"]:
        temporary = archive.with_suffix(".part")
        print(f"Downloading {name}", flush=True)
        for attempt in range(3):
            try:
                request = urllib.request.Request(metadata["url"], headers={"User-Agent": "SDRPlusPlus-Android"})
                with urllib.request.urlopen(request, timeout=120) as response, temporary.open("wb") as output:
                    shutil.copyfileobj(response, output)
                if digest(temporary) != metadata["sha256"]:
                    raise ValueError(f"SHA-256 mismatch for {name}")
                temporary.replace(archive)
                break
            except (OSError, ValueError) as error:
                if attempt == 2:
                    raise RuntimeError(f"Cannot download {name}: {error}. Check HTTPS_PROXY or Android Studio's network settings.") from error
                time.sleep(2 ** attempt)
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as source:
        for member in source:
            relative = Path(*Path(member.name).parts[1:])
            target = (destination / relative).resolve()
            if not target.is_relative_to(destination.resolve()):
                raise ValueError(f"Unsafe archive entry: {member.name}")
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as content, target.open("wb") as output:
                    shutil.copyfileobj(content, output)
    marker.write_text(metadata["sha256"])
    return destination


def patch_codec2(source):
    filename = source / "src/CMakeLists.txt"
    text = filename.read_text()
    beginning = text.find("# when crosscompiling we need a native executable")
    ending = text.find("# codebook.c", beginning)
    if beginning >= 0:
        text = text[:beginning] + text[ending:]
        text = text.replace("COMMAND generate_codebook ", 'COMMAND "${PYTHON_EXECUTABLE}" "${SDRPP_CODEBOOK_SCRIPT}" ')
        text = text.replace("DEPENDS generate_codebook ", 'DEPENDS "${SDRPP_CODEBOOK_SCRIPT}" ')
        filename.write_text(text)


def copy_headers(name, source, build, prefix):
    locations = {
        "zstd": [(source / "lib", "zstd*.h", "")],
        "fftw": [(source / "api", "fftw3.h", "")],
        "volk": [(source / "include/volk", "*.h", "volk"), (build / "include/volk", "*.h", "volk"), (source / "kernels/volk", "*.h", "volk")],
        "libusb": [(source / "libusb", "libusb.h", ""), (source / "libusb", "libusb.h", "libusb-1.0")],
        "airspy": [(source / "libairspy/src", "*.h", "libairspy")],
        "airspyhf": [(source / "libairspyhf/src", "*.h", "libairspyhf")],
        "hydrasdr": [(source / "libhydrasdr/src", "*.h", "libhydrasdr")],
        "hackrf": [(source / "host/libhackrf/src", "*.h", "libhackrf")],
        "rtlsdr": [(source / "include", "*.h", ""), (build / "src", "rtlsdr_export.h", "")],
        "codec2": [(source / "src", "*.h", "codec2"), (build, "codec2/version.h", "codec2")],
        "libxml2": [(source / "include/libxml", "*.h", "libxml2/libxml"), (build / "libxml", "*.h", "libxml2/libxml")],
        "libiio": [(source, "iio.h", "")],
        "libad9361": [(source, "ad9361.h", "")],
    }
    for directory, pattern, subdirectory in locations[name]:
        destination = prefix / "include" / subdirectory
        destination.mkdir(parents=True, exist_ok=True)
        for header in directory.glob(pattern):
            shutil.copy2(header, destination / header.name)


def run(command, environment, logfile):
    with logfile.open("a", encoding="utf-8") as output:
        output.write("\n" + subprocess.list2cmdline([str(value) for value in command]) + "\n")
        output.flush()
        result = subprocess.run(command, env=environment, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        tail = logfile.read_text(encoding="utf-8", errors="replace").splitlines()[-80:]
        raise RuntimeError("\n".join(tail) + f"\nFull build log: {logfile}")


def build_dependencies(arguments):
    cache = arguments.cache.resolve()
    prefix = arguments.prefix.resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    (prefix / "lib").mkdir(exist_ok=True)
    mako = fetch("mako", cache) / "mako"
    if not mako.exists():
        mako = cache / "sources" / ("mako-" + SOURCES["mako"]["sha256"]) / "lib"
    else:
        mako = mako.parent
    markupsafe = fetch("markupsafe", cache) / "src"
    python_paths = [str(mako), str(markupsafe), str(fetch("volk", cache) / "gen")]
    environment = os.environ.copy()
    environment["PYTHONPATH"] = os.pathsep.join(python_paths + [environment.get("PYTHONPATH", "")])
    if Path(sys.executable).resolve().is_relative_to(cache / "tools"):
        configuration = next(Path(sys.executable).parent.glob("python*._pth"))
        python_zip = configuration.with_suffix(".zip")
        configuration.write_text("\n".join([python_zip.name, "."] + python_paths + ["import site", ""]), encoding="utf-8")
    common = [
        "-G", "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={arguments.ninja.as_posix()}",
        f"-DCMAKE_TOOLCHAIN_FILE={arguments.ndk.as_posix()}/build/cmake/android.toolchain.cmake",
        f"-DANDROID_ABI={arguments.abi}",
        f"-DANDROID_PLATFORM={arguments.platform}",
        "-DANDROID_STL=c++_shared",
        "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_SHARED_LIBS=ON",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}",
        f"-DCMAKE_PREFIX_PATH={prefix.as_posix()}",
        f"-DCMAKE_PROJECT_INCLUDE={ROOT.as_posix()}/unversioned.cmake",
        f"-DPYTHON_EXECUTABLE={Path(sys.executable).as_posix()}",
        "-DENABLE_TESTING=OFF",
        "-DTHREADS_PTHREAD_ARG=-pthread",
        "-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=OFF",
    ]
    usb_options = [
        f"-DLIBUSB_LIBRARIES={prefix.as_posix()}/lib/libusb1.0.so",
        f"-DLIBUSB_LIBRARY={prefix.as_posix()}/lib/libusb1.0.so",
        f"-DLIBUSB_INCLUDE_DIR={prefix.as_posix()}/include",
        f"-DLIBUSB_INCLUDE_DIRS={prefix.as_posix()}/include",
        "-DLIBUSB_FOUND=ON", "-DLIBUSB_VERSION=1.0.25",
    ]
    options = {
        "zstd": ["-DZSTD_BUILD_PROGRAMS=OFF", "-DZSTD_BUILD_TESTS=OFF", "-DZSTD_BUILD_STATIC=OFF", "-DZSTD_BUILD_SHARED=ON"],
        "fftw": ["-DENABLE_FLOAT=ON", "-DBUILD_TESTS=OFF"],
        "volk": ["-DVOLK_CPU_FEATURES=OFF", "-DENABLE_ORC=OFF", "-DENABLE_MODTOOL=OFF", "-DENABLE_STATIC_LIBS=OFF"],
        "libusb": [],
        "airspy": usb_options + ["-DDISABLE_USB_DEVICE_DISCOVERY=ON"],
        "airspyhf": usb_options + ["-DDISABLE_USB_DEVICE_DISCOVERY=ON"],
        "hydrasdr": usb_options + ["-DDISABLE_USB_DEVICE_DISCOVERY=ON", "-DENABLE_STATIC_LIB=OFF"],
        "hackrf": usb_options + ["-DDISABLE_USB_DEVICE_DISCOVERY=ON"],
        "rtlsdr": usb_options + ["-DBUILD_UTILITIES=OFF", "-DDETACH_KERNEL_DRIVER=ON", "-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON"],
        "codec2": ["-DUNITTEST=OFF", "-DLPCNET=OFF", f"-DSDRPP_CODEBOOK_SCRIPT={ROOT.as_posix()}/codebook.py"],
        "libxml2": ["-DLIBXML2_WITH_LZMA=OFF", "-DLIBXML2_WITH_PYTHON=OFF", "-DLIBXML2_WITH_ICONV=OFF", "-DLIBXML2_WITH_ZLIB=OFF", "-DLIBXML2_WITH_PROGRAMS=OFF", "-DLIBXML2_WITH_TESTS=OFF"],
        "libiio": ["-DWITH_TESTS=OFF", "-DWITH_EXAMPLES=OFF", "-DWITH_USB_BACKEND=OFF", "-DWITH_SERIAL_BACKEND=OFF", "-DWITH_LOCAL_BACKEND=OFF", "-DWITH_IIOD=OFF", "-DHAVE_DNS_SD=OFF", "-DWITH_DOC=OFF", "-DENABLE_PACKAGING=OFF", f"-DLIBXML2_LIBRARY={prefix.as_posix()}/lib/libxml2.so", f"-DLIBXML2_INCLUDE_DIR={prefix.as_posix()}/include/libxml2"],
        "libad9361": [f"-DLIBIIO_LIBRARIES={prefix.as_posix()}/lib/libiio.so", f"-DLIBIIO_INCLUDEDIR={prefix.as_posix()}/include"],
    }
    subdirectories = {"zstd": "build/cmake", "airspy": "libairspy", "airspyhf": "libairspyhf", "hydrasdr": "libhydrasdr", "hackrf": "host/libhackrf"}
    signature = hashlib.sha256(json.dumps(common + arguments.components).encode())
    signature.update((arguments.ndk / "source.properties").read_bytes())
    for filename in sorted(ROOT.rglob("*")):
        if filename.suffix in (".py", ".cmake", ".json", ".txt"):
            signature.update(filename.read_bytes())
    for name in arguments.components:
        source = fetch(name, cache)
        if name == "codec2":
            patch_codec2(source)
        build = cache / "build" / arguments.abi / name
        build.mkdir(parents=True, exist_ok=True)
        target_name, library_name = LIBRARIES[name]
        library = prefix / "lib" / f"lib{library_name}.so"
        signature.update(json.dumps(SOURCES[name]).encode())
        signature.update(json.dumps(options[name]).encode())
        fingerprint = signature.hexdigest()
        stamp = build / ".complete"
        if stamp.exists() and stamp.read_text() == fingerprint and library.exists() and (prefix / "include").exists():
            continue
        print(f"Building {name} ({arguments.abi})", flush=True)
        logfile = build / "build.log"
        logfile.write_text("")
        project = source / subdirectories.get(name, "")
        extra = []
        if name == "libusb":
            project = ROOT / "libusb"
            extra = [f"-DLIBUSB_SOURCE={source.as_posix()}"]
        run([str(arguments.cmake), "-S", str(project), "-B", str(build)] + common + options[name] + extra, environment, logfile)
        run([str(arguments.cmake), "--build", str(build), "--target", target_name, "--parallel", str(arguments.jobs)], environment, logfile)
        candidates = list(build.rglob(library.name))
        if len(candidates) != 1:
            raise RuntimeError(f"Expected one {library.name} in {build}, found {len(candidates)}")
        shutil.copy2(candidates[0], library)
        copy_headers(name, source, build, prefix)
        stamp.write_text(fingerprint)
    print(f"Android dependencies ready: {prefix}", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, required=True)
    parser.add_argument("--ninja", type=Path, required=True)
    parser.add_argument("--abi", choices=["arm64-v8a", "armeabi-v7a", "x86", "x86_64"], required=True)
    parser.add_argument("--platform", default="android-28")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 8))
    parser.add_argument("--components", nargs="+", choices=list(LIBRARIES), required=True)
    build_dependencies(parser.parse_args())


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
