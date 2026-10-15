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
The minimum SDK is 28 (Android 9); the compile and target SDK are 36.
Release lint remains enabled without a baseline or target-SDK suppression.
Android 15 and newer use explicit window insets to keep the native UI clear of
the status bar and display cutouts. Legacy storage permissions are only requested
on older Android versions; recording to shared storage uses the system folder
picker instead.

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

By default, APKs include `arm64-v8a`, `armeabi-v7a`, `x86` and `x86_64`.
To limit the build to one ABI, set this in your personal Gradle properties or
pass it on the command line:

```powershell
.\gradlew.bat :app:assembleDebug -Psdrpp.abis=arm64-v8a
```

Both Debug and Release package native modules as `lib<module>.so` on Android.
Non-debuggable Android installations only extract native libraries with the
`lib` prefix; the runtime module list uses the same names. Desktop module names
are unchanged. Verify a built APK from the repository root with:

```powershell
python core/tests/android_apk.py android/app/build/outputs/apk/release/app-release-unsigned.apk
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

## USB hotplug

Connecting a supported USB SDR while the app is open requests Android USB access
immediately, unless access has already been granted. Permission callbacks remain
registered for the Activity lifetime, including after denial or a previous grant.
Unplugging clears the old descriptor and allows a fresh request after replugging.
The USB attachment intent also lets Android offer SDR++ when the app is closed;
an existing Activity handles the intent without starting a second native instance.
The device filter matches the core's Airspy, Airspy HF+, HackRF, HydraSDR and
RTL-SDR VID/PID lists, avoiding permission prompts for unrelated peripherals.
Authorizing another device does not replace a currently opened SDR connection.

## Multi-channel operation

New Android configurations default to 200% UI scaling. Existing saved scaling
preferences are preserved; change **Display > High-DPI Scaling** to use a different
value. The loading logo, title and progress message are fitted and centered
inside the current viewport in either orientation, independently of UI scaling.

The menu defaults to horizontal tabs: only the selected category's controls are
shown. Tap once to switch categories, or drag the tab strip horizontally to
reveal overflow tabs without changing the selection. A tab-list picker also
provides direct access to every category; the selected
category is remembered. Its content scrolls independently of the tab strip.
Landscape windows place the menu on the left; portrait windows place it below
the spectrum/waterfall. The toolbar menu button hides either layout. To use the
old collapsible sections instead, disable **Tabbed menu** in **Display**.

On Android, the button to the right of **Lock** switches between portrait and landscape:
**Landscape** enters landscape from portrait, and **Portrait** switches back.
The selected orientation is remembered across restarts. Until the first manual
selection, the app follows the system orientation policy. The old independent
orientation-lock setting is no longer used. The main **Lock** holds the current
orientation and disables this button; unlocking restores the selected direction.
Rotation keeps the existing activity/native receiver alive and does not
intentionally stop the SDR or recordings. Portrait keeps the status bar visible;
landscape hides it, and menu placement follows the existing responsive layout.

**Keep screen on**, below the Lock/orientation row, independently prevents the
foreground app's screen from timing out. It defaults to off and is remembered
across restarts. It does not lock tuning, playback or orientation. The main
**Lock** still forces the screen to stay on and temporarily disables this
checkbox; unlocking restores the independent preference. When the option is off
and the session is unlocked, normal system screen timeout applies. This uses a
window flag, not a background wake lock, and does not keep the display awake
after switching to another app.

The **Multi Receiver** menu adds independent demodulation and per-channel audio/IQ
recording. Android recording directories are selected through the system folder
picker, with persistent access grants. See
[the multi-channel guide](../misc_modules/multi_receiver/README.md) for monitoring,
locking, recording formats, error handling and Frequency Manager channel groups.

## Constellation and IQ-only channels

The **Constellation** menu has a **Show constellation** switch, scatter/density
views and a larger window. It supports raw IQ plus manual/automatic PSK and square
QAM inspection plus binary FSK detection/frequency-level display, with optional
blind symbol-rate estimation and manual override. Editing the manual rate
automatically turns off automatic estimation.
Auto detection and explicit CDR/DAB detection modes also inspect compatible
OFDM timing, including CDR digital sidebands around central analog FM and DAB
periodic null symbols. These are candidates only: no broadcast audio/service
decoding or recovered OFDM constellation, and DAB versus DAB+ is not distinguished.
Use **Free (IQ only)** in Radio or Multi Receiver for arbitrary channel bandwidth
without audio. Channel bandwidth is shared by reception, constellation input and
per-channel IQ recording. See [the constellation guide](../misc_modules/constellation/README.md)
for automatic-estimation assumptions and limitations.
