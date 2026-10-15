# Constellation and digital signal inspection

The **Constellation** menu follows the selected Radio/RX channel, including a
channel in **Free (IQ only)** mode. It is included in desktop and Android builds.
Existing configurations receive a default instance on upgrade.

## Display

The waterfall has two touch-sized drag controls directly above its frequency
scale. Drag **Frequency** left/right to move the selected channel, or drag
**Bandwidth** to resize it without moving its center. Taps on these controls do
not change values. Tapping the spectrum or waterfall only selects a channel;
it no longer tunes to the touched frequency. The frequency scale can still be
dragged to pan, and the numeric frequency entry remains available. Session and
recording locks disable both controls; fixed-bandwidth modes disable resizing.

- Check **Show constellation** to show the plot and start its analysis stream.
  Uncheck it to hide the plot, close its larger view and release the analysis
  stream. Visibility and settings are remembered across restarts.
- **Follow selected channel** follows the channel selected in the waterfall;
  uncheck it to select a fixed channel. Selecting a channel here does not tune it.
- **Larger window** opens a resizable plot with a close button. Closing this window
  leaves the inline plot available until **Show constellation** is unchecked.
- **Freeze display** freezes plotted points, not the SDR, recordings or live
  estimates. **Clear / restart** clears the plot and restarts synchronization.
- Select scatter points or **Density view**, a square equal-scale I/Q plot with
  positive Q upwards, a grid and optional ideal-reference crosses. The layout
  takes inspiration from OnAir's constellation scatter/density views:
  <https://github.com/adswill/OnAir> (`app/plots.cpp`, `app/analysis_tabs.cpp`).
- Point count (128?4096), RMS normalization, mean removal, gain and phase rotation
  affect visualization only. The reference grid is a guide, not a measurement.

The channel has **one bandwidth setting**. The Radio/RX channel, IQ recording and
constellation input all use it; the viewer has no independent bandwidth control.
Use Free mode to select a wider or narrower channel without audio demodulation.
Free mode preserves this bandwidth when moving the channel. At a capture edge,
the channel position is constrained so its entire passband remains in range;
it is not silently narrowed. A full-capture-width channel stays at the capture
center. Dragging is clamped immediately at the input setter, not corrected on the
next frame, so continuing to drag outward does not move the selection beyond the
edge. Bandwidth grips also stop at the capture edge without moving the channel
center. Reducing the source's effective sample rate can still reduce a bandwidth
that no longer fits the capture at all.
Display gain/rotation, matched filtering and recovery never modify recorded IQ.
Analysis and Free-mode processing prefer power-of-two front-end decimation rates
that cover the selected bandwidth. Dragging a channel edge therefore does not
create an enormous rational-resampling filter bank for each arbitrary width.
These internal sample rates do not quantize the selected channel bandwidth.
Very narrow channels below the power decimator's range use a low-rate rational
stage instead, keeping the channel filter bounded as well.

## Processing and automatic estimates

**Raw IQ** shows channelized samples before demodulation, without symbol or carrier
recovery. A digital signal can form a ring or cloud in this mode; that alone does
not identify its modulation.

**Auto detect** supports candidates BPSK, QPSK, 8PSK, square 16/64/256-QAM and
binary FSK / CPFSK-like signals.
Manual selections for these constellations are also available. Recovery applies
an RRC matched filter, AGC, Gardner symbol timing, an Nth-power FFT carrier
estimate and a Costas loop. Auto mode compares the appropriate recovery branches.
The selected RRC rolloff should approximate the transmitter's pulse shaping.
FSK instead uses a phase-difference frequency discriminator and transition-clock
analysis. Detection requires two consistent windows, coherent transitions at
integer symbol intervals, varied run lengths and two separated frequency levels
at symbol centers. It can estimate the clock even when the IQ envelope is
constant. There is also a manual **2-FSK / CPFSK-like** processing choice.

For detected FSK, the plot shows normalized **frequency levels**, not an IQ
constellation: -1 and +1 represent the fitted low and high frequencies. Offset,
deviation and frequency spread are displayed separately. Display rotation, mean
removal and RMS normalization are bypassed in this view. This is family-level
detection, not proof of a particular Gaussian pulse shape, protocol or data lock.

With **Estimate symbol rate** enabled, the viewer searches the squared-envelope
spectrum for periodic features. It requires two consistent estimates. The search
range is approximately the analysis IQ sample rate divided by 64 through divided
by 2, matching the recovery range of 2?64 samples per symbol. Acquisition can take
several windows. Uncheck **Automatic symbol rate** to override it manually in
baud, or edit **Manual rate** directly: editing automatically disables automatic
estimation. The active manual/estimated rate is explicitly shown. A channel
narrower than rate x (1 + rolloff) now produces a PSK/QAM warning rather than
preventing recovery; that RRC bandwidth rule is not applicable to FSK.

Modulation estimation compares normalized constellation fits, radial moments and
cluster occupancy. At least 1024 recent recovered symbols are required. **Unknown**
is retained for insufficient evidence, incompatible constellations, noise and
unresolved rings. A candidate score is a **heuristic fit score, not a calibrated
probability**; the RMS fit residual is not a calibrated receiver EVM measurement.

Important limitations:

- This is a signal-inspection aid, not a universal automatic demodulator or a
  protocol decoder. It does not report verified lock, BER or protocol identity.
- Symbol-rate features can be weak or absent (for example very low rolloff,
  constant envelope), confused with harmonics, or contaminated by interference.
  A periodic feature can also come from a non-digital signal. A rate estimate is
  therefore a candidate, not proof of modulation; manual override is available.
- Low SNR, fading, frequency error, insufficient capture bandwidth and overlapping
  signals may prevent acquisition or give a wrong candidate. Higher-order QAM
  needs particularly good signal quality. There is no adaptive channel equalizer.
- Multi-level FSK, generic OFDM, APSK, non-square QAM and other unsupported
  modulation families are not decoded or reliably identified. CDR/DAB-compatible
  OFDM timing candidates have the additional limitations described below.
- Binary FSK detection is conservative: it needs enough transitions with varied
  run lengths, 2-64 IQ samples per symbol, and frequency states away from the
  discriminator's Nyquist ambiguity. Constant data, a fixed alternating pattern,
  severe filtering, or short bursts can remain Unknown. GFSK/CPFSK variants are
  not individually identified, and audio or protocol payloads are not decoded.
- Analysis adds CPU/RAM load. Hide the viewer if the device cannot sustain it.
  Hiding or changing the viewer does not intentionally stop SDR/audio/IQ capture.

## CDR and DAB broadcast candidates

**Auto detect** also checks broadcast timing when the selected channel has a
suitable bandwidth. Explicit **CDR detection (FM hybrid / digital)** and
**DAB / DAB+ detection** modes avoid single-carrier recovery entirely. Their
processing does not depend on the manual baud rate or RRC rolloff. Neither mode
decodes services, station names, audio, FEC or payloads, nor does it change the
Radio/RX demodulator or recorded IQ.

- CDR: select Free mode and a 40-650 kHz channel, centered on a digital subband
  or the complete broadcast. About 500 kHz covers a typical full hybrid layout.
  GY/T 268.1-2013 transmission modes 1-3 are checked using their useful-symbol
  and cyclic-prefix durations (816 kHz reference clock). FFT filtering separately
  inspects the center and digital bands around +/-125, +/-175 and +/-225 kHz.
  Short overlapping timing windows tolerate a beacon/subframe boundary.
- In hybrid CDR, an analog FM station can indeed occupy the center while digital
  OFDM occupies sidebands. This is defined by the spectrum allocation, not an
  accidentally adjacent station. Spectrum modes 9 and 10 in appendix B illustrate
  this arrangement; digital-only allocations also exist. The central FM alone
  must not count as CDR, and the detector does not claim to identify analog
  simulcast merely from the strength of its center spectrum.
- DAB: center the complete ensemble and use 1.536-2.5 MHz bandwidth; about
  1.7 MHz is recommended to allow filter transition margins. All four numerologies
  in ETSI EN 300 401 V1.4.1 table 38 are checked. Detection requires both cyclic
  prefix timing and repeated low-power null symbols at the corresponding frame
  interval. Mode I uses a 1 ms useful symbol, 504/2048000 s guard interval and
  96 ms frame. DAB and DAB+ cannot be separated by these physical-layer features;
  distinguishing their audio coding requires service/payload decoding.

Both detectors require compatible results in two analysis windows and clear the
candidate when evidence is lost. The displayed score is heuristic evidence,
**not** a probability, BER, CRC result or verified protocol lock. CDR currently
matches OFDM numerology rather than its beacon/service information, and unrelated
OFDM with exactly the same timings can remain a compatible candidate. The plot
still shows **raw IQ**, not a recovered QPSK/QAM subcarrier constellation.

Analysis runs in the IQ worker, at most twice per second, with bounded capture
windows and a maximum analysis rate of 6.144 MS/s. It is deliberately not a scan
for every broadcast inside a many-megahertz channel: select the broadcast channel
first. Narrowband DAB, badly centered/cropped sidebands, very strong interference,
short bursts or severe multipath can remain Unknown. Hiding the viewer releases
the detector along with its existing analysis stream.

Parameter references:

- NRTA GY/T 268.1-2013, table 1 and appendix B:
  <https://www.nrta.gov.cn/module/download/downfile.jsp?classid=0&filename=df21296086c84684b680de5c29892812.pdf>
- ETSI EN 300 401 V1.4.1, section 14.1, table 38:
  <https://www.etsi.org/deliver/etsi_en/300400_300499/300401/01.04.01_60/en_300401v010401p.pdf>

## Validation

`core/tests/broadcast.cpp` checks CDR modes 1-3, isolation of digital CDR from a
strong central FM signal, DAB modes I-IV, periodic null requirements, non-native
sample rates, acquisition/loss and noise/tone/FM/FSK/wrong-prefix rejection. It
builds as standalone C++17 with `-Icore/src`. Android additionally exercises the
production recovery class in Auto and both explicit broadcast modes.
`core/tests/constellation_viewer.cpp` verifies that broadcast modes bypass the
single-carrier manual-baud input checks and release their analysis streams.

`core/tests/constellation.cpp` tests bounded history, freezing, normalization,
rotation, all six modulation candidates, unknown rejection and symbol-rate
feature estimation, constant-envelope binary FSK clock estimation/manual rates,
and rejection of noise, tones, analog FM and multi-level FSK. It builds as a
standalone C++17 program with `-Icore/src`.
On Android it additionally uses production DSP/ VOLK and RRC-shaped synthetic
signals with carrier offset to test manual and automatic carrier/timing recovery
and symbol-rate estimation for every supported modulation.

These synthetic tests do not replace real-radio, multipath, low-SNR or Android
touch/layout acceptance testing.
