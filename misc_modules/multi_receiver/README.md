# Multi-channel reception and recording

The **Multi Receiver** menu controls multiple Radio instances within the current
SDR capture range. A source capable of the requested bandwidth is still required:
this does not turn a narrow-band receiver into a 10 MHz receiver. There is a
32-channel safety limit; practical capacity depends on CPU, RAM, USB and storage.

## Receive and monitor

Each channel has one **Bandwidth** value, used both by the demodulator's channel
filter and its independent IQ recording channelizer. Constellation analysis also
follows that same value. IQ sample rate is a sampling-rate setting, not a second
bandwidth setting.

**Free (IQ only)** is the arbitrary-bandwidth / no-audio mode. Its width can be
set from 1 Hz up to the available SDR capture span around the channel center;
unlike WFM it is not capped at 250 kHz. It drains channel IQ without generating
audio and does not start an audio output stream. Choose IQ recording for these
channels; audio recording is disabled. Free-mode frequencies and bandwidths are
saved in both bookmarks and channel groups. Ordinary modes retain their existing
mode-specific bandwidth limits. Changing mode can therefore clamp the one shared
bandwidth to the new mode's supported range.

1. Select the source sample rate and start the SDR.
2. Open **Multi Receiver**, choose **Add channel**, and set the frequency,
   bandwidth and demodulation mode for each `RX` channel. Values are in Hz.
3. Use the individual `RX` Radio menus for stereo, deemphasis and other
   mode-specific settings.
4. Select **Mix**, **Single channel** or **Mute**. In Mix mode, check **Include in
   mix** on the wanted channels. In Single mode, select **Listen to**.

Each channel has an independent demodulator. Monitoring uses the audio backend's
shared output mixer, with a conservative 1/N linear gain for N selected channels.
All channels should use the same physical audio output. Unselected streams,
including the original Radio, are suppressed only for monitoring; their recording
taps are upstream of monitoring volume. Thus solo/mute does not remove samples
from a recording. With no managed channels, normal output routing is restored.

Center tuning follows only the selected VFO, including when changing the
selection. Retuning its frequency compensates the offsets of other VFOs so their
absolute frequencies and bandwidths stay unchanged. Updating an unselected VFO
does not recenter the view. When all passbands cannot fit around the requested
center, the capture center and view are constrained to preserve the other
channels; a frequency that cannot coexist in the shared capture is rejected.
Session and recording tuning locks still prevent retuning.

## Record each channel

On Android, press **...** beside the directory to open the system directory
picker. Grant access to a directory, preferably on local device storage. The
persistable directory grant is retained across restarts. Actual files are created
through Android's Storage Access Framework, not by guessing a filesystem path.
Desktop builds retain the native folder picker.

For each channel, select **Record audio**, **Record IQ**, or both, then press
**Record channel**. **Record selected tracks** starts the selected tracks of all
channels. Each track has separate progress counters and a separate file series.

- Audio is stereo PCM16 WAV at the channel's audio sample rate.
- IQ is two-channel Float32 WAV, I followed by Q. A dedicated channelizer translates,
  filters and resamples only that channel, independently of its audio demodulator.
- IQ sample rate must cover the channel bandwidth and be no greater than
  the source's effective sample rate. Leave some transition-band margin.
  It is adjusted to cover a changed channel bandwidth; at the full capture width,
  the sample rate can equal the bandwidth and there is no extra edge margin.
- Filenames contain the channel name, tuning frequency, timestamp, track type and
  segment number. Each track rolls over before approximately 1 GiB of payload,
  avoiding the classic RIFF/WAV 4 GiB limit. Counters display payload bytes.
- Writes use a bounded queue. Open, seek, write, flush, finalization, queue overflow
  and five-second no-sample failures produce visible, persistent errors rather than
  an apparently healthy recording. A global banner also appears when menus are
  collapsed. A failure on one channel does not intentionally stop the others.
- Stopping drains queued samples in the background and displays **Finishing
  files...** until finalization is complete. A five-second lack of write progress
  produces a warning even if storage is stuck and the queue has not filled yet.
- WAV headers are checkpointed and flushed approximately once per second of
  recorded samples. This reduces damage after interruption but is not a guarantee
  against power loss or the OS forcibly killing the process.

Active channel recordings hold tuning, demodulation and bandwidth settings fixed
so recorded metadata stays consistent. Stop them before changing those settings
or loading a different group. Directory changes apply to future recordings only.

The original **Recorder** records full-band IQ rather than a selected channel.
Its IQ recordings also hold tuning settings, including source sample rate and
front-end decimation, until stopped. This does not change the original Recorder
into a channel-selective IQ recorder; use Multi Receiver for that.

Providers without seekable file descriptors, such as some cloud providers, are
rejected explicitly because WAV headers require seeking. Full-device permission,
storage capacity, device disconnects and OS background restrictions can still
prevent recording. This feature does not add an Android foreground service.

## Lock

The top-bar **Lock** checkbox prevents retuning, bandwidth/source-setting changes,
stopping the SDR and manually stopping active recorders. On Android it also locks
the current screen orientation, keeps the display awake and ignores Back until
unlocked. Output selection and volume can still change. Recording errors may stop
  the failed track even while locked: a lock cannot override a lost device or failed
storage. Unlock before normal shutdown; process termination cannot be blocked.

## Frequency groups

Open **Frequency Manager ? Multi-channel groups** to save or replace a named
group, load a group or delete a saved group. Groups store the capture center,
channel frequencies, modes, bandwidths, IQ sample rates, recording-track
selections and monitoring selection. They do not automatically start recording.
Advanced Radio settings continue to be stored by the Radio module per channel.

Existing frequency bookmarks are preserved. Select several bookmarks and choose
**Open selected bookmarks as channels** to receive them together. The group must
fit inside the current source sample rate; otherwise an explicit error is shown
and the existing channels are retained. Source hardware and sample rate are not
changed automatically. Current channel setup and recording directory are also
saved in `multi_receiver_config.json`; named groups live in
`frequency_manager_config.json`.

## Validation

USB and LSB IQ files use the selected passband's center, not the carrier edge;
the frequency in each IQ filename identifies that center.

`core/tests/recording_io.cpp` is a standalone C++17 test for the file/RIFF layer.
Compile it with `core/src/utils/riff.cpp` and `core/src/utils/recording_file.cpp`,
then pass a test-output directory as its only argument.

`core/tests/channel_capture.cpp` links against `sdrpp_core` and exercises the real
WAV writer and asynchronous capture code with synthetic samples, including
parallel audio/IQ, segmentation, restart, overflow, no-data timeout and session
guards. On Android it can run as a separate NDK executable without installing or
stopping the GUI application. Neither test validates radio hardware reception,
system-picker UI behavior or orientation changes.
An optional second argument points to `multi_receiver.so` and additionally checks
group validation, rejection without changing existing state, locked loading and
deferred group application.
