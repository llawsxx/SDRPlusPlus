# Building SDR++ for Android

Open this `android` directory in Android Studio, select a Gradle JDK 17 or newer
(the bundled JDK is suitable), sync, then build or run the `app` configuration.
Use an Android Studio version compatible with Android Gradle Plugin 9.2.

The checked-in Gradle Wrapper uses Gradle 9.4.1. Android Gradle Plugin provides
the Kotlin compiler; a separate Kotlin Gradle plugin is not required.

## Android SDK components

Install these from Android Studio's SDK Manager, enabling **Show Package Details**:

- Android SDK Platform 36
- Android NDK (Side by side) 28.2.13676358
- CMake 3.22.1

Android Studio records the SDK location in the untracked `local.properties`.
No absolute SDK path is checked into the project. Keep the SDK licenses accepted
so Gradle can install missing SDK components automatically.

## Command line

With `JAVA_HOME` pointing to a JDK 17 or newer, run from this directory:

```powershell
.\gradlew.bat :app:assembleDebug
```

On Linux/macOS use `bash ./gradlew :app:assembleDebug`. These hosts also need
Python 3.9 or newer. On Windows a checksum-verified portable Python is downloaded
automatically; no Python, pip, Git, Docker, WSL, MSVC or Unix build tools are
required for native compilation.

The signed debug APK is `app/build/outputs/apk/debug/app-debug.apk`.
Release builds still require your own release signing configuration.
The minimum and target SDK remain 28 to preserve existing app behavior.

## Native dependencies

CMake automatically prepares the dependencies during its first configuration,
including Android Studio's native project sync. It downloads pinned source
archives, checks SHA-256, and builds the libraries with the Android NDK and Ninja.
It then builds the SDR++ core and enabled modules normally. The existing Android
features remain enabled, including Airspy, Airspy HF+, HydraSDR, HackRF, RTL-SDR,
PlutoSDR and the M17 decoder. Optional desktop-only modules are not enabled.

Dependencies include ZSTD, FFTW, VOLK, libusb, the SDR drivers, Codec2, libxml2,
libiio and libad9361. Mako and MarkupSafe are fetched as source-only Python modules
for VOLK code generation. Codec2 tables are generated with a portable Python
helper instead of requiring a second host C compiler.

`native/sources.json` locks source URLs, commits/versions and checksums. The
Android USB-aware HackRF and RTL-SDR forks used by the existing CI are retained.
Shared libraries have unversioned Android SONAMEs and are imported as CMake
targets, so Gradle packages them together with the SDR++ libraries.

Generated files are cached under the ignored `android/deps` directory:

- `downloads` and `sources`: shared source archive cache
- `tools/python`: portable Python on Windows
- `build/<ABI>/<dependency>/build.log`: individual dependency build logs
- `install/<ABI>`: headers and shared libraries used by SDR++

The cache is reused across Debug/Release builds and survives Gradle `clean`.
Changes to the dependency scripts, locked sources, NDK or ABI invalidate the
relevant build stamps. Source preparation is locked to avoid concurrent ABI
builds modifying the shared cache. Delete `android/deps` to rebuild everything.

The default ABI is `arm64-v8a`. To build other ABIs, set this in your personal
Gradle properties or pass it on the command line:

```powershell
.\gradlew.bat :app:assembleDebug -Psdrpp.abis=arm64-v8a,armeabi-v7a,x86,x86_64
```

The first build needs internet access and takes substantially longer than
incremental builds. Configure Android Studio's HTTP proxy for Gradle/Maven as
needed. Native source downloads use the standard `HTTPS_PROXY`/`HTTP_PROXY`
environment variables; set these before launching Android Studio. A SOCKS-only
proxy must expose an HTTP proxy endpoint for Python's standard-library downloader.

For standalone CMake builds, automatic dependency building is
opt-in with `-DSDRPP_ANDROID_BUILD_DEPS=ON`. Without it the existing
`/sdr-kit/<ABI>` layout is still supported. A different prebuilt prefix can be
selected with `-DSDRPP_ANDROID_DEPS_ROOT=/path/to/prefix`.
The old Docker image's Gradle 7 toolchain cannot build the upgraded Android project;
the Android CI job now uses the same Gradle Wrapper and source-build path as the IDE.
