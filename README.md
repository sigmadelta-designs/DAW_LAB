# DAW_LAB

A plugin host sandbox (JUCE 8.0.4, C++20). Scans AU/VST3 plugins and runs a serial
chain of them: device input -> plugin 1 -> ... -> plugin N -> device output (MIDI input
goes to every plugin that accepts it). The chain order is shown top to bottom in the
window; each row has Bypass / Edit / Up / Down / Remove. With an empty chain, input
passes straight to output.

The app icon (`Assets/AppIcon.png`, wired in via `ICON_BIG` in CMakeLists.txt) is a small eye diagram: one
crossing of an NRZ eye, drawn with the same raised-cosine edges the Signal Generator uses, in the app's own
orange/blue trace colours. `Assets/generate_icon.cpp` is the one-off JUCE program that drew it (not part of
the app build; its own header comment says how to build and run it) - edit and re-run that to restyle it.

## Hover tips

Every control on every stage - built-in or plugin - has a hover tip with its name, a coloured **DUT**
(blue) or **SIM** (amber) tag, and why you'd reach for it:

- **DUT** - the control mirrors a real, physical knob, register or property of the transmitter, channel
  (PCB/cable/backplane) or receiver you'd be testing: line rate, FFE taps, CTLE boost, trace length,
  termination resistors, DFE taps, CDR loop gains.
- **SIM** - the control only exists because this is a simulation: which test pattern or stress condition
  you chose (PRBS pattern, injected jitter, injected noise, a mistuned reference clock), or a setting of
  the measurement/display itself (the eye scope's persistence, vertical scale, or view; an S-parameter
  channel's FIR tap count).

Momentary actions (Load file, Auto-solve, Restart, Reset counters, board-drawing tools) get a plain
tip instead, since "DUT or SIM" doesn't apply to a button you click rather than a value you set.
The rendering lives in `Source/Builtin/ControlTip.h` (the DUT/SIM encoding) and
`Source/Builtin/DawLabLookAndFeel.h` (the tooltip's look); every custom editor owns one of each plus a
`juce::TooltipWindow`, so this also works when a stage is loaded as a standalone plugin in another DAW.
The Signal Generator, Lossy Channel and Noise Injector stages - previously shown with JUCE's generic,
un-tooltipped parameter list - now have small dedicated editors (`GeneratorEditor`, `LossyEditor`,
`NoiseEditor`, built on a shared `SimpleParamEditor` base) so their controls get tips too.

## SerDes / TxRx stages (built in)

The chain list starts with the built-in stages below. They are ordinary chain members (bypass, reorder, presets all work),
and share one **line rate** and **NRZ / PAM4** setting, chosen in the main window. Real time is scaled by 1,000,000:
**1 GBd on the wire runs as 1 kBd here, and a 5 GHz Nyquist tone is 5 kHz.** Every signal is a differential pair:
**R = +, L = -**; the eye scope looks at R - L.

Typical chain: `Signal Generator -> FFE -> Lossy Channel -> Eye Scope` (put the FFE after the channel to model an RX FFE).

**Register resolution.** On real silicon, none of a receiver or TX FFE's knobs are continuous values -
they're register fields driving a DAC or an analog-shape selector, a fixed number of discrete codes over a
fixed range. Every control that models one quantizes to that automatically and always (not a setting - it
models the hardware, not a choice about the simulation): FFE and DFE taps to 64 codes (6-bit), CTLE boost
and DC gain to 32 codes (5-bit), CTLE zero/second-pole and the CDR's Kp/Ki loop gains to 16 codes (4-bit,
these are typically a small bank of selectable corners/gear-shifted gains on real silicon rather than a fine
DAC), the AGC target level to 128 codes (7-bit), and the DFE's LMS adaptation step to 16 codes (4-bit,
approximating the power-of-two "mu select" fields real sign-sign LMS loops often use). This happens at the
point each value is read for real use (`getEffectiveTaps()`, `getCtleSettings()`, `processBlock()`), so
what gets applied and what each readout shows are both already the quantized value - there's no separate
"ideal" number hiding underneath. Auto-adapt (FFE training, CTLE auto-adapt) sees the same quantized
landscape a real link-training loop would, including its occasional consequence: a coordinate search can
converge to a slightly worse code if the ideal value sits between two quantization steps
(`Source/Builtin/RegisterResolution.h`). The sliders themselves snap too - each quantized parameter's own
range has its interval set to the register's step size, so dragging one can only ever land on a legal code,
and the number in its text box always matches what's actually applied (`RegisterResolution::quantize()`
uses the exact same snap formula as `juce::NormalisableRange`, so the two can't disagree even at an exact
tie, e.g. quantizing a tap's 0.0 default on a symmetric range that has no code sitting exactly at zero).
Every one of these sliders also shows a small "bits" readout under or beside it - the control's actual
register contents, as raw bits plus the code and its maximum (e.g. `101010 (42/63)`) - so it's visible
at a glance that you're really setting a fixed-width digital field, not reading back a continuous number.

- **Signal Generator** - PRBS7/9/11/13/15/23/31, clock 1010, clock 11001100; NRZ or PAM4 (Gray coded); amplitude and
  edge time (raised-cosine, in UI). Ignores its input. `Drive`: differential (default), + leg only, - leg only, or common-mode. **Jitter** is applied to each edge:
  *random* RJ (Gaussian, UI rms) and *deterministic* SJ (sinusoidal, UI pk-pk, frequency in real-world MHz, so 5 MHz runs as 5 Hz)
  and DCD (duty-cycle distortion: rising edges late, falling edges early, UI pk-pk). Runs are repeatable (fixed seed).
- **FFE** - 7 symbol-spaced taps (pre3 pre2 pre1 main post1 post2 post3), fractional-sample delays, so tap spacing is exactly
  1 UI at any line rate. `TX mode` forces main = 1 - sum|other taps| (constant swing, so equalising costs low-frequency
  amplitude, like a real transmitter). `Reverse tap direction` mirrors pre <-> post. Negative post/pre taps = de-emphasis.
  The editor shows the applied taps, DC/Nyquist gain, the boost, and compares it to the channel's loss.
  **Auto-adapt** (link training): the FFE steps its own taps and keeps a change only if the *decision error measured
  downstream* (the Eye Scope's probe) improves - like a TX FFE trained through a back-channel. It needs no knowledge of the
  channel and works before or after it, but needs an Eye Scope later in the chain. Takes about 15-20 s (audio time) from flat,
  reports "Converged", and retrains by itself if the link changes (channel loss, noise, rate).
- **Lossy Channel** - two-pole low-pass set so the channel loses N dB at Nyquist (half the line rate). Has an
  `Enabled` switch (off = signal passes untouched, and the FFE editor stops counting its loss). Meant as a stand-in for
  a real trace/channel model later; any such stage can report its loss through `LinkSettings::reportChannelLoss`.
- **Trace Channel** - two PCB traces you draw, in place of the simple Lossy Channel. **R (+) runs through the + trace and
  L (-) through the - trace**, each with its own delay and loss set by its length: delay = length x propagation delay
  (170 ps/inch default, adjustable) and loss = length x (0.25 sqrt(f) + 0.12 f) dB/inch at the link's Nyquist frequency
  (FR4-like, `Loss x FR4` scales it). Equal lengths = no skew; a length difference is **skew** (shown in ps and in UI at the
  current line rate) and a **loss imbalance**. Its editor is a 8 x 5 inch board on a 0.05" grid:
  click to place corners (45/90 degree routing, clearances enforced, no acute turns, traces can't cross), click near an OUT
  pad to finish, right-click or Backspace to remove a corner. The drawing is saved with the chain preset. Unfinished traces count
  as a straight line to the pad. Has an `Enabled` switch like the Lossy Channel.
  - **Coupling / crosstalk**: where the + and - traces run parallel and close they couple (the board highlights it; the
    weight falls off with spacing, from 1.0 at the 0.1" minimum to nothing beyond 0.6"). Coupled lines carry two
    modes: the **odd mode** (differential) at the base speed and the **even mode** (common) a little slower - here by
    2% of the coupled, spacing-weighted delay, times the `Coupling x` setting. In R/L terms that *is* crosstalk: drive one
    trace and the other gets the odd/even difference (FEXT), and a skewed or unequal pair converts differential signal
    into common-mode. A perfectly balanced, equal-length pair driven differentially shows none of it, which is the
    physics, not a bug - use the generator's **Drive** option (`+ leg only`, `- leg only`, `Common-mode`) to see it.
  - **Aggressor lanes** (game mode, 1-3 depending on difficulty): dashed magenta lines on the board that belong to noisy
    neighbours. They are not obstacles - a trace may cross one - but running alongside one couples its data in. Each lane is
    a live PRBS stream at its own rate, differentiated (coupling responds to dV/dt) and injected into each trace in proportion
    to *that trace's* spacing-weighted parallel run beside it (`Aggressor x` scales it, default 0.03 FS rms per weighted inch).
    Consequences you can see: a square crossing couples nothing; a long parallel run couples a lot; the nearer trace picks up
    more, so an uneven pass becomes **differential** noise (the eye), while an even pass is mostly **common-mode** (invisible
    to a differential probe). The editor shows each lane's coupling to + and -, the differential pickup (mFS rms and
    signal-to-crosstalk in dB) and the common-mode pickup. The game adds a crosstalk budget (relative to what the solver
    achieves, tighter with difficulty), and **Auto-solve routes around the lanes** (it pays a length cost for parallel
    runs beside them: about 85% less differential pickup for about 3% more length on average).
  - **External aggressor input**: the stage's bus is actually 4 channels, not 2 - channels 3/4 are an optional real
    aggressor input, there for whatever is placed earlier in the chain and happens to drive them (e.g. a second Signal
    Generator positioned so its own output reaches this far unchanged - every other built-in stage today is 2 channels,
    so nothing currently drives them without deliberately engineering a 4-wide path to here). **Use external aggressor
    input** (off by default - every existing preset is unaffected) and **External aggressor coupling** (the same
    spacing-weighted-inches units the drawn lanes use, hand-set since there is no drawing to derive it from here) couple
    whatever real signal is actually present, injected as a pure differential disturbance. Left disconnected, or with
    the toggle off, it has no effect at all - verified directly (0.000 FS measured deviation in both those cases, vs. a
    real, correctly-sized disturbance only when both the toggle is on and something is actually driving it). Useful for
    things the synthetic per-lane model can't represent: a specific real pattern instead of canned PRBS23, or jitter
    correlated between the victim and the aggressor.
  - **Mode measurements** (the *Modes* block, at the link's Nyquist frequency, mixed-mode S-parameters with the usual
    unitary normalisation): odd-mode delay and insertion loss (Sdd21), even-mode delay and loss (Scc21), differential-to-common
    conversion (Sdc21, = Scd21) and the crosstalk when one trace is driven. They are computed from the model and agree
    with a sine measured through the audio path to within a few tenths of a dB.
  - **Reflections**: each trace is a lattice (one section per sample of delay) whose impedance profile comes from the drawing:
    closer to the other trace than the nominal 0.3" pair spacing lowers it, further apart raises it (up to +/-25%), corners dip it
    (45 degrees 2%, 90 degrees 5%), and the first and last quarter inch (pad / connector) are offset by `Connector %`. The two
    ends are closed with `Source ohm` and `Load ohm` (per leg; 50 = matched). Echoes appear at the round-trip delay of every
    discontinuity, including the repeated bounces between a source and a load that both mismatch, and the trace's loss is
    applied inside the line so distant echoes are attenuated more. `Reflect x` scales the geometry-derived deviations
    (0 = uniform 50 ohm line; terminations still apply). With everything matched it is sample-for-sample the plain
    delay-and-loss line. The **Settings** tab holds all the sliders; the **TDR** button swaps the game panel for a TDR trace
    (impedance seen looking into each trace vs distance); the readouts give S11 (return loss) at Nyquist and the largest
    reflections with their positions. Lines longer than 1024 sections (very long traces at high sample rates) fall back to the
    plain path.
  - **Game mode**: `New game` scatters random keep-out obstacles (difficulty 1-5 = more obstacles, tighter budgets) and
    guarantees the board is solvable. Route both traces around them, keeping skew inside the budget (0.30 ... 0.07 UI) and the
    average length inside a multiple of *par* (what the solver achieves). One star for solving, two for within 15% of par,
    three for within 5% and half the skew budget. Length-matching is done the way real boards do it: meanders on the shorter trace.
  - **Auto-solve** routes both traces (shortest path, then adds meanders to the shorter one until the skew is within about
    0.02"/3 ps) and marks the board "auto-solved - no score".
- **S-Parameter Channel** - a channel defined by a Touchstone file (VNA measurement or field-solver export), for when you have
  real data. `.s2p`: one trace, S21 applied to both legs. `.s4p` and up: two coupled traces, with `Ports` choosing which are
  the + and - inputs and outputs (P1,P2 -> P3,P4 or P1,P3 -> P2,P4); all four ways through are applied, so skew, loss
  imbalance, mode conversion and crosstalk come from the data. Reads Touchstone 1: Hz/kHz/MHz/GHz, RI/MA/DB, the 2-port
  S11 S21 S12 S22 ordering, multi-line records; S-parameters only. Frequencies are real-world (1 GHz plays as 1 kHz). The
  editor plots Sdd21 / Scc21 / Sdc21 against frequency with the link's Nyquist marked, the step response of the filters that
  are actually running, and the numbers at Nyquist (loss, skew, mode conversion). The file is embedded in the stage state, so
  chain presets carry their channel (about 270 KB for a 500-point 4-port). Method: bulk delay is removed, the smooth remainder
  interpolated, DC extrapolated, and the response tapered to zero above the file's last point, then transformed to a
  512/1024/2048-tap FIR per path (measured against an analytic channel: 0.001 dB magnitude, 0.000 rad phase). Limits: it is the
  channel between matched 50 ohm ports, so transmitter/receiver reflections are not included (use the Trace Channel for those);
  the delay plus 24 samples of design margin must fit the FIR length (the editor warns); frequency spacing must be fine enough
  to resolve the delay (phase step under pi per point).
  `Channels/` has three synthetic examples: `fr4_8in_coupled_pair.s4p`, `backplane_12in_via_stub_notch.s4p` (a resonance
  near 6 GHz) and `fr4_5in_single_trace.s2p`.
- **Noise Injector** - White, Pink (1/f), Brown (1/f^2), Blue, Violet, Gray (equal-loudness: inverse A-weighting),
  DC offset, Sine interference (frequency in real-world MHz), Crosstalk (PRBS aggressor at an offset rate, coupled
  through a differentiator). `Level` is the RMS added (for DC, the offset). *Differential* injection adds to R - L; *Common-mode*
  adds to both legs, which the differential eye scope does not see. Coloured types are defined on the audio frequency axis.
- **Receiver** - the receive side: CTLE, AGC, CDR, DFE and slicer in one stage.
  - **CTLE**: one zero and two poles; `boost` (0-18 dB) sets the peaking above DC gain (the first pole sits `boost` above the zero), the high pole
    rolls it off. The editor plots the gain against frequency with the link's Nyquist marked. `Auto-adapt boost` hill-climbs the boost against the
    slicer error (in a noise-free simulation more boost always looks better, so it can run to the limit; with noise it backs off).
  - **AGC**: brings the signal to a fixed target level `T` (sign-sign LMS on the slicer error), so the slicer levels are fixed at +-T (PAM4: +-T and +-T/3).
  - **CDR**: an NCO produces sampling instants (a data sample, and an edge sample half a UI earlier). A bang-bang **Alexander** phase detector compares
    the edge sample with the neighbouring decisions and nudges the clock early or late (`Kp`, mUI per decision); an integral term (`Ki`) tracks
    frequency offset. It works on the sign bit, so it recovers PAM4 too. `Reference offset (ppm)` detunes the receiver's clock so you can watch the CDR
    pull in (it learns the opposite offset: measured -990 ppm for +1000, +2970 for -3000, zero errors). Lock, learned ppm, edge level, slicer error and
    AGC gain are shown.
  - **DFE**: five post-cursor taps (units of `T`), sign-sign LMS on the slicer error. The adapted values are written back to the sliders so they save with
    the preset.
  - **Slicer**: NRZ, or PAM4 with Gray decoding. The recovered bits are logged.
  - **Output** (what the next stage, e.g. an Eye Scope, sees): the CTLE/AGC analog signal; the **slicer input** (analog with the DFE feedback applied, i.e.
    the post-DFE eye); or the **regenerated data** (retimed clean NRZ/PAM4 at the recovered clock, at level `T`).
  - **Sequence comparison** (bottom of its editor): the generator logs the bits it sends and the receiver the bits it recovers. The comparison engine finds
    the alignment on its own (the offset between the two streams), then checks every received bit against the bit that should have arrived, and
    shows the two sequences side by side with disagreements in red, plus bits compared, errors, BER (or a 95%-confidence upper bound when there are
    none), bits since the last error, error bursts and how many times the alignment was lost (a cycle slip or the link falling over makes it search
    again). Counting starts once aligned, so acquisition is not scored. `Reset counters` starts over.
  - Measured: locks and runs error-free on a clean link, on a 19 dB channel that is unreceivable without equalisation (CDR never locks) with CTLE 12 dB +
    DFE, on PAM4, and with 0.03 UI rms RJ plus 0.25 UI pk-pk SJ; the regenerated output is a clean eye at the target level.
- **Eye Scope** - pass-through probe (the **Pass-through** switch: on, the signal goes on to the next stage bit-for-bit unchanged; off, the scope
  terminates it like a scope on a 50 ohm load instead of a T-adapter - it still measures, nothing is passed on). `Feed FFE trainer` says whether
  its decision-error probe drives the FFE's auto-adapt (turn it off on a second scope, or when the receiver is doing the training). The **View** selector shows the *differential* (odd mode, R - L), *common-mode* (even mode,
  (R + L) / 2), or either leg on its own (handy for watching the quiet leg of a crosstalk test); a readout always gives odd- and
  even-mode RMS and peak-to-peak and their ratio. Persistence eye diagram over 2 UI, timing recovered from the zero crossings
  (Re-lock if you change something upstream), plus eye height / width readouts and the decision error (NMSE, dB).
  Open its editor to see the eye; the probe runs (and the FFE can train against it) even with the editor closed.
- **Jitter / Phase Noise Scope** - the timing counterpart to the Eye Scope: same pass-through/View controls, but measures
  the *time* each data edge actually crosses instead of the *voltage* a sample lands on. Zero crossings are found the
  same way the Eye Scope's timing recovery does (Catmull-Rom interpolation + a locked reference phase), but here each
  crossing's deviation from an ideal, evenly spaced clock becomes one time-interval-error (TIE) sample. That reference
  clock isn't fixed: it tracks phase and frequency with a loop bandwidth deliberately far below anything the spectrum
  plot can show (a few Hz vs. tens of Hz for its lowest FFT bin at a typical link rate) and over-damped (no peaking) -
  like a real jitter analyzer's own internal reference, which is built to be transparent to the jitter it measures,
  not to track it the way a receiver's CDR does. Its editor shows a scrolling TIE strip chart, a jitter histogram,
  RMS/pk-pk jitter in real-world picoseconds, and a Welch-averaged jitter/phase-noise spectral density plot (log
  frequency) that calls out the single strongest periodic component (frequency + RMS-equivalent amplitude) when one
  stands out above the noise floor - so a sinusoidal jitter (SJ) source shows up as a line at its own frequency
  instead of just inflating the RMS number the way it would on a simple jitter meter. A "Spectral integral check" line
  cross-checks the two measurement paths against each other (Parseval's theorem: the PSD integrated over frequency
  should equal the direct time-domain variance) - a good way to see the math is self-consistent, not just trust the
  numbers. Verified against synthetic waveforms with known injected jitter (`Source/Builtin/JitterAnalyzer.cpp`):
  measured RMS and detected tone frequency/amplitude matched the true injected values to within 1-3%, and a
  jitter-free, SJ-free random-only signal correctly reported no dominant tone.

Receiver presets: `NRZ_10G_24_rx_ctle_dfe_lossy_19dB` (a 19 dB channel the receiver equalises with CTLE 12 dB and the DFE; zero errors), `NRZ_10G_25_rx_trace_game21_ctle_autotrain`
(the CTLE trains itself on load), `PAM4_10G_4_rx_pam4_6dB`, and `NRZ_10G_26_rx_noisy_ber_demo` (noise and jitter chosen to give a measurable BER, about 2e-3, so the
comparison panel has errors to show). In each the FFE is flat: the receiver does the equalising.

More receiver presets (chain: generator, FFE, channel, noise, receiver, eye scope; 10 GBd; the eye is the receiver's slicer input, BER over about 60 000 bits, measured live):

| Preset | What it shows | Result |
|---|---|---|
| `NRZ_10G_27_rx_all_off_lossy_12dB` | 12 dB channel, light noise, receiver equalisation off | BER 1.5e-3, eye closed |
| `NRZ_10G_28_rx_ctle_only_lossy_12dB` | same, CTLE 9 dB only | no errors, eye h 13% |
| `NRZ_10G_29_rx_dfe_only_lossy_12dB` | same, DFE only (taps adapt) | no errors, eye h 33%, tap 1 = +0.44 |
| `NRZ_10G_30_rx_ctle_plus_dfe_lossy_12dB` | same, CTLE 6 dB + DFE | no errors, eye h 33%, tap 1 = +0.28 (the CTLE takes some of the ISI, so the DFE needs less) |
| `NRZ_10G_31_tx_ffe_plus_rx_lossy_19dB` | 19 dB, equalisation split between the transmit FFE and the receiver | no errors, eye h 73% |
| `NRZ_10G_32_rx_ctle_autotrain_lossy_16dB` | CTLE trains itself on load (about 4 s) | no errors, eye h 70% |
| `NRZ_10G_33_rx_trace_reflections_dfe_off` / `_34_..._dfe_on` | drawn trace with mismatched terminations; the DFE removes the echo | eye h 24% -> 48% |
| `NRZ_10G_35_rx_aggressors_game21_lane_aware` / `_36_..._lane_blind` | noisy neighbour lanes, routed around vs. ignored | eye h 72% vs 3% |
| `NRZ_10G_37_rx_sparam_fr4_8in_ctle_dfe` / `_38_rx_sparam_via_stub_notch_dfe` | Touchstone channels | eye h 77% / 44% |
| `NRZ_10G_39_rx_cdr_tracks_sinusoidal_jitter` | 0.2 UI sinusoidal jitter, the CDR follows it | no errors |
| `NRZ_10G_40_rx_cdr_tracks_1000ppm_offset` | reference clock 1000 ppm off, the CDR follows it | no errors |
| `NRZ_10G_41_rx_cdr_too_slow_loses_lock` | 3000 ppm offset with the CDR gains turned down | never locks, BER about 0.4 |
| `PAM4_10G_5_rx_sparam_fr4_8in_ctle_dfe`, `PAM4_10G_6_rx_trace_game21_ctle_dfe`, `PAM4_10G_7_tx_ffe_plus_rx_lossy_8dB` | PAM4 through an S-parameter channel, a solved trace, and TX FFE + receiver | no errors |

The exact eye-height/width figures above (and in "What the eye shows" below) were measured before FFE/CTLE/
DFE register resolution was added and can drift a few points now that taps and boost are quantized - spot-
checked after adding it and every pass/fail result above still held (zero-error presets stayed zero-error),
but the specific percentages are approximate, not exact, for presets measured before that change.

### What the eye shows (10 GBd NRZ, 12 dB channel; `Presets/`, images in `docs/eyes/`)

| FFE setting | Boost vs 12 dB loss | Eye height | Eye width | What you see |
|---|---|---|---|---|
| none (`NRZ_10G_1_no_EQ`) | 0 dB | 14% | 64% | slow edges, rails fatten into several bands, crossings smear sideways |
| under-equalised (`..._2_under_EQ`) | +3 dB | 28% | 75% | same picture, less severe; still fat rails, vertical squeeze |
| matched (`..._3_good_EQ`) | +11 dB | 83% | 86% | thin single rails, clean crossings, wide open eye (at a smaller swing) |
| over-equalised (`..._4_over_EQ`) | +19 dB | 26% | 68% | overshoot/pre-shoot on edges, rails split again from ringing, small eye |
| wrong sign (`..._5_wrong_sign`) | -15 dB | 0% | 0% | extra low-pass: eye closed |
| reversed direction (`..._6_reversed`) | +11 dB | 27% | 51% | boost is right but lands on the wrong side of the cursor: skewed/tilted eye, smeared crossings |

Eye height is the tightest opening at the sampling instant; as % it is relative to the ideal opening for the observed swing.
Presets were generated from the least-squares (zero-forcing) taps for this channel's measured pulse response.
The auto-adapt presets (`NRZ_10G_10_auto_adapt`, `PAM4_10G_3_auto_adapt`) start flat and train themselves on load;
NRZ ends at about 90% eye height, slightly better than the least-squares taps because it optimises the decision error directly.

Trace-channel presets (game board #7, FFE auto-adapt on): `NRZ_10G_12_trace_game7_matched_autoEQ` (solved, zero skew),
`NRZ_10G_13_trace_game7_skew_0p5UI_autoEQ` (same board, one trace 0.5 UI longer: the FFE fixes the loss but the eye width still
suffers from the skew), and `NRZ_10G_14_trace_game7_to_solve_autoEQ` (empty traces - draw them yourself).

Reflection presets: `NRZ_10G_20_trace_reflections_mismatched_autoEQ` (Rs 35, RL 80, +15% connectors) vs `NRZ_10G_21_trace_matched_no_reflections_autoEQ`
(the same board matched): with the FFE trained the eye is about 76-84% open against 89%.
S-parameter presets: `NRZ_10G_22_sparam_fr4_8in_pair_autoEQ` (eye about 89%) and `NRZ_10G_23_sparam_via_stub_notch_autoEQ` (the notch costs
loss and width: about 72-80% high, 69% wide).

Aggressor presets (game board #21, difficulty 5, FFE auto-adapt on): `NRZ_10G_17_aggressors_game21_lane_aware_autoEQ` (auto-solved,
crosses the lanes square: eye about 90% open), `NRZ_10G_18_aggressors_game21_lane_blind_autoEQ` (the same board routed shortest-path
ignoring the lanes: the eye is closed even after the FFE has trained), and `NRZ_10G_19_aggressors_game21_to_solve_autoEQ` (empty, draw it yourself).

Mode / crosstalk presets: `NRZ_10G_15_trace_skewed_common_mode_view` (a skewed board with the scope on the common-mode view) and
`NRZ_10G_16_crosstalk_plus_only_minus_leg_view` (only the + trace driven, scope watching the quiet - trace).

More presets: `NRZ_10G_7_good_EQ_jitter` (RJ + SJ + DCD), `NRZ_10G_8_good_EQ_white_noise`, `NRZ_10G_9_good_EQ_crosstalk`,
`NRZ_10G_11_no_channel` (channel disabled: a clean, jitter-free reference eye).

## DDR stages (built in) - first pass, DDR now, HBM to follow

A second, independent domain alongside the SerDes stages, sharing the same app shell, hover-tip/homework
system, and plugin-format machinery, but with its own bus and its own shared link object
(`DdrLinkSettings`, not `LinkSettings` - a DDR chain has no use for a line rate/modulation choice, and a
SerDes chain has no use for a chip-select bus). `BuiltInProcessor` is now a template
(`BuiltInProcessorBase<LinkSettingsType>`) parameterised on which link object a stage carries and how many
channels its bus needs, specifically so this could be added without touching any of the 9 existing SerDes
stages (`BuiltInProcessor` itself is just `BuiltInProcessorBase<LinkSettings>`, unchanged from their point
of view).

**The bus** (`Source/Builtin/DdrBus.h`) is one byte lane, modelled with real differential pairs where DDR
actually has them (CK+/-, DQS+/-, matching this project's existing R+/L- convention) and one real wire per
supported chip/rank for chip select (one-hot, up to 4 - the same way a real DIMM wires CS, not an encoded
value): CK, 4x CS, a simplified command code, an address, 8x DQ, DQS. Command/address is deliberately a
simplified stand-in for DDR's real multi-cycle CA bus (just enough to drive a write/read/idle protocol for
training purposes, not a literal JEDEC DDR4/DDR5 command encoding), and "one data beat per bus cycle" is a
placeholder for DDR's real double-pumped-per-clock timing.

**DDR Controller** (`lab.ddrcontroller`) drives the bus and runs a small, continuously-repeating built-in
self-test (BIST): write a pattern across an address sweep (All zero / All one / Address as data /
Checkerboard), then read it back and compare, counting mismatches live - the same "run forever, show a
live error count" idea as the SerDes side's BitCompare, just for memory words instead of a bitstream.
**Target chip** picks which CS wire it asserts.

**DDR Chip** (`lab.ddrchip`) is the memory side. Its **Chip number** is literally which CS wire it's
strapped to (`DdrBus::csChannel(chipNumber)`) - the same kind of choice a real package/rank-select pin is -
and it only ever responds while that wire is asserted, ignoring everything else on the bus exactly like
real hardware. Writes are genuine signal-level simulation: a DQS edge is the capture clock, and each DQ bit
is sliced against a trainable **Vref**, a real, common DDR mis-training bug. It never modifies or clears
the bus - a real multi-drop bus has every chip just listen and pass the signals on, which is what lets
several `DdrChip` stages (one per rank) sit one after another in an ordinary chain, the way several ranks
share one real bus. Its editor (the first GUI anywhere in the DDR domain) shows the memory array
"physically": one row per address, one column per DQ bit, a literal 0/1 digit per cell, live as the BIST
writes and reads it - the most direct reading of what this simulation actually stores ("rows and columns"
here means address x bit-position, not a model of a real DRAM's row/column/bank addressing, which this
simulation doesn't represent). Only the rows actually on screen are drawn, so scrolling through all
`DdrChip::memoryWords` (4096) stays cheap even though a typical BIST sweep only ever touches a few dozen of
them. Also exposes **Chip number** and **Vref** directly, plus the physical properties below, and a status
line (selected-at-least-once / never-selected, plus live words-written/words-read counts).

**DDR Chip's own trainable physical properties** (added alongside Homeworks 20-25 below, mapped to a real
DDR/LPDDR5 validation sequence) each have a genuine signal-level consequence on the same write-capture path,
the same discipline as every other DAW_LAB control:

- **ODT target (ohm)** and **ZQ cal error (%)** model on-die termination and ZQ calibration. ZQ calibration
  trims a chip's actual termination against an external 240 ohm reference; `effectiveOdt = odt * (1 +
  zqerror/100)`, and the mismatch between that and the bus's nominal impedance (`DdrBus::nominalImpedanceOhms`,
  a representative 60 ohm) becomes a real reflection coefficient, `Gamma = (Z-Z0)/(Z+Z0)` - the same formula
  the SerDes side's Trace Reflections uses, simplified here to a single tap: a scaled echo of the signal
  fed back roughly one unit interval later. A single bounded reflection (|Gamma| < 1) can never by itself
  flip an otherwise perfect, full-swing, noiseless bit against a dead-centre Vref - a real finding from this
  pass's verification harness, not an assumption - so a measurable ODT/ZQ homework always pairs it with a
  Vref that isn't trivially centred (exactly how real Vref training interacts with whatever reflection
  environment is actually present).
- **CS timing error (UI)** models CS/CA training: how many unit intervals late this chip's own chip-select
  sampling lags the real signal (look-back only, to avoid needing to simulate the future). Untrained CS
  timing can miss a short, individually-addressed command (like a read) outright, while a long write burst
  (CS held continuously for many UI) mostly still gets through - a real and useful distinguishing symptom.
- **DQ0-7 skew (UI)** (`dqskew0..7`) are per-bit deskew: all eight equal models write levelling left undone
  (a byte-wide DQS-to-CK/lane skew, corrupting essentially every word); only some of them nonzero models
  per-bit write-DQ deskew training left incomplete (a sparser, data-pattern-dependent symptom - only
  addresses where that specific bit happens to differ from the previous word's same bit are wrong). The
  `DdrChipEditor` grid shows a thin colour-coded indicator under each DQ column header (grey at 0, reddening
  towards the 0.5 UI range limit) so a mis-trained pin is visible at a glance without needing eight more
  sliders.
- **DFE enabled** is a single shared-tap decision-feedback equalizer that cancels the *known* ODT/ZQ
  reflection using the chip's own prior decision - the same sign-sign idea (simplified from DDR5's real
  4-tap-per-pin reality, and simplified further to use the known Gamma directly rather than adapting to it,
  since the lesson here is "DFE on vs. off", not convergence speed) as this project's existing SerDes
  receiver DFE.

**DDR Lossy Channel** (`lab.ddrlossy`) is this bus's counterpart to the SerDes side's LossyChannel, and
just as deliberately a placeholder: one **DQ/DQS swing** parameter attenuates only those signals (the ones
an eye actually forms on and a Vref is trained against), leaving CK/CS/CMD/ADDR untouched. Enough to make
"a real channel has loss, and Vref has to be trained for the swing it actually sees" a genuine, measurable
scenario (see Homework 18 below) without yet modelling trace-length skew, reflections, or a fly-by/
multi-drop topology.

**DDR Eye Scope** (`lab.ddreyescope`) is the DDR-domain counterpart to the SerDes side's Eye Scope, letting
a chain check a real eye diagram on **DQ0-7 or DQS** - directly answering the question of whether DQ/DQS
can be examined like any other eye in this tool. A genuinely different stage rather than the existing
EyeScope stretched to cover both domains, for three real reasons: DQ bits are single-ended (one real wire
each, no second leg to form a differential odd-mode signal from the way the SerDes side's two-channel bus
always has), DQS *is* a true differential pair so **Probe** can pick either one specific DQ bit or DQS
(`dqsP - dqsN`), and there is no DDR-side equivalent of EyeScope's "Feed FFE trainer" probe (nothing on the
DDR side adaptively trains against this measurement, and `DdrLinkSettings` carries no feedback object the
way `LinkSettings::probe` does) - so that whole mechanism is simply absent, not stubbed out. Reuses
`EyeAnalyzer` completely unchanged (it was already generic: a sample stream, a unit-interval length, a PAM4
flag, nothing else).

Two real, non-obvious things the verification pass for this stage found, not assumed going in:
- DQ/DQS are only actually driven during a write burst - the controller holds them flat at 0 the rest of
  the time (reads don't drive DQ back out at all, see Phase 1/Phase 2 below), which is correct bus
  behaviour but would badly dilute a naively-captured eye (measured height dropped to ~50% in testing)
  if those long idle stretches were fed to the analyzer along with real traffic. Fixed by only measuring
  and forwarding samples when the command bus actually reads `write`, letting the sample index jump across
  each idle stretch - the FIFO reader's existing gap-detection (any index step > 1.5) already splits that
  into separate contiguous bursts automatically, the same mechanism it already used for ordinary FIFO drops.
- DQS toggles every **half** of a DQ unit interval (it's the strobe, not the data), so probing it needs the
  eye analyzer's own "one symbol period" halved, or its zero-crossing clock recovery locks to the wrong
  rate and the eye collapses to nothing (measured 0% height/width before this fix, a clean, fully locked
  93-100% eye after). The readout's "UI = ... samples" line reflects this - half the data UI specifically
  when Probe is set to DQS, with a note explaining why.

A separate, non-bug nuance surfaced the same way: a specific DQ bit's own eye can show "Locking to the data
edges..." indefinitely under a BIST pattern that happens to toggle that particular bit only rarely (e.g.
DQ3 under the default "Address as data" pattern only flips once every 8 addresses) - confirmed by switching
to the Checkerboard pattern (every bit toggles every address) and watching the same bit lock cleanly to a
full eye in the same test. Real lesson, not a tool limitation: which BIST pattern to run depends on which
pin's margin you're actually trying to see, the same as choosing a transition-rich pattern (an MPR-based
one, say) for real per-bit DQ training rather than assuming any pattern reveals every pin's eye equally.

**Phase 1 / Phase 2 - a deliberate, documented simplification.** DDR's DQ bus is bidirectional (the
controller drives it on writes, the chip drives it back on reads), but this project's whole existing
paradigm is a strictly feed-forward plugin chain - there's no clean way to make a chip "talk back upstream"
without a graph cycle, which needs a host-level change this pass didn't make. So for now: the **write**
path is real signal-level simulation (a channel stage and an eye/jitter scope placed between the controller
and a chip mean something real there), and a **read**'s answer is fetched directly from a small shared
object (`DdrLinkSettings::Readback`, tagged with which address it's actually answering - see the long
comment on it for why that tagging matters) rather than driven back out through DQ/DQS and a simulated
channel. True electrical loopback for reads is a planned next step, not an oversight.

Verified directly (not just by inspection) before trusting it, the same discipline used everywhere else in
this project: a console harness caught and fixed two real bugs before the logic was right - (1) a race
where the controller's CAS-latency countdown could resolve its read compare before the addressed chip had
even processed that block yet (several bus cycles happen inside one audio block; a chip's processBlock for
a block runs strictly after the controller's), fixed by polling for a reply tagged with the actual address
asked for, bounded by a timeout, instead of trusting a fixed UI count; and (2) the controller re-reading
its "target chip" parameter live on every cycle, so changing it while a read was still in flight could
retarget the poll to a different chip's reply slot mid-transaction, fixed by latching the target only when
a command actually issues. After both fixes: chip-select gating is clean (an unaddressed chip, even with a
badly wrong Vref, is never touched), the BIST genuinely catches a real fault (an attenuated signal with a
stale Vref measured ~95% word errors, not a tautological always-pass), and a real chained topology
(Controller -> Chip0 -> Chip1, addressing Chip1) works correctly.

**Not done yet, by design, in this pass:** GUI editors for the Controller or Lossy Channel stages (DDR
Chip has one now - parameters work on the other two, there's just no window yet); a DDR-side Jitter Scope
(DDR Eye Scope now exists, above, covering the eye-diagram half of this gap; a jitter/phase-noise view of
DQS specifically would reuse `JitterAnalyzer` the same way, and is a natural next step rather than done);
a fly-by/multi-drop topology or per-trace length skew in DDR Lossy Channel
(DDR Chip's own ODT/ZQ reflection, above, is a single-tap, chip-local simplification, not a real
multi-drop transmission-line model); real CA sampling against a trained Vref(CA) (this project's CMD/ADDR
bus carries whole values held constant for a UI rather than a serialised, thresholded signal, so CS/CA
training is represented narrowly as a CS-timing-only skew - see Homework 22); read-path training (read
gate/receive-enable training and read DQ per-bit deskew/centering both need the Phase 2 electrical
loopback below, which doesn't exist yet); and duty-cycle training (this project's "one data beat per bus
cycle" placeholder has no independent rising/falling-edge timing for duty-cycle distortion to act on -
needs the real double-pumped-per-clock timing upgrade first). Vref's own 2D (Vref x timing) joint-training
nature is covered already, if not literally as a 2D sweep, by Homework 18. Standalone VST3/AU packaging (an
18-channel bus is unusual for a generic DAW insert) and HBM, which was always meant to follow DDR once this
foundation was solid rather than be built in parallel, also remain future work. Homework scenarios (17-25)
do exist now - see "Homework" below.

## Chain presets

Save Preset... / Load Preset... write `.labchain` files (XML, default folder
`~/Documents/DAW_LAB/Presets`). A preset stores, per chain slot and in order: the plugin
identity, bypass flag, the plugin's own state blob, and the normalised value of every
parameter. Two instances of the same plugin keep separate settings. Plugins that can't be
found on load are skipped and named in the status line.

Parameter values are stored as well as the state blob because a plugin whose
`getStateInformation()` is a stub (all the vst_lab plugins currently are) would otherwise
lose its settings. A plugin's non-parameter state only persists if the plugin implements
its state methods.

## Homework: presets with something wrong

`Presets/HOMEWORK_*.labchain` are exercises: each one is a chain with something realistic and deliberately
wrong in it, modelled on an actual SerDes bring-up/debug scenario rather than an arbitrary bug. Load one
(Load Preset...) and try to find the problem using the tool's own measurements (eye, TDR, CTLE/DFE/CDR
status, BER) before checking the **Homework Hint...** button, which opens a window with:

- the scenario, framed as a lab ticket - always visible;
- **Reveal hint** - a nudge, not the fix;
- **Reveal explanation** - the root cause and how a real debug session would confirm and fix it.

Both stay hidden until clicked, so opening the window to check whether a preset has homework attached
doesn't spoil the exercise. The window updates (and re-hides anything already revealed) whenever you load a
different chain.

| Preset | Scenario |
|---|---|
| `HOMEWORK_01_closed_eye_no_tx_eq` | TX FFE left flat against an 18 dB channel - the "is the board bad?" false alarm |
| `HOMEWORK_02_ffe_wrong_direction` | Real FFE tap values, applied backwards (Reverse on) - worse than no EQ at all |
| `HOMEWORK_03_missing_termination` | Receiver-end termination effectively missing - reflections that look fine on one shot, bad with real traffic |
| `HOMEWORK_04_cdr_too_slow` | CDR loop gains too low to track reference-clock jitter - the receiver won't hold lock |
| `HOMEWORK_05_aggressor_crosstalk` | Trace routed hugging a noisy neighbour lane - fails only when it's active |
| `HOMEWORK_06_ctle_overboost` | CTLE boost cranked far past what the channel needs - amplifies noise more than signal |
| `HOMEWORK_07_bad_connector` | Both terminations matched; the echo is from a mid-trace connector/via mismatch instead |
| `HOMEWORK_08_margin_stackup` | No single broken setting - several individually-mild impairments stack up past the BER budget |
| `HOMEWORK_09_dfe_wrong_sign` | A hand-entered DFE tap with the wrong sign - fighting ISI instead of cancelling it |
| `HOMEWORK_10_stale_calibration` | TX FFE and RX CTLE both still tuned for a board revision that no longer exists - double over-equalization |
| `HOMEWORK_11_sparam_wrong_ports` | A real measured Touchstone file, read with the wrong port-pair mapping for how the fixture was cabled |
| `HOMEWORK_12_wrong_probe_point` | Receiver's Output set to the pre-DFE node - the eye looks unconvincing but the BER is actually fine |
| `HOMEWORK_13_dfe_step_too_large` | DFE adaptation step too large - it hunts around the optimum instead of settling |
| `HOMEWORK_14_emi_coupled_jitter` | A specific periodic jitter tone riding on the signal - the eye looks only a little soft, the Jitter Scope's spectrum shows a clear spike |
| `HOMEWORK_15_jitter_tolerance_exceeded` | The same sinusoidal jitter amplitude as a passing test, at a much higher offset frequency - the CDR's perfectly normal gains simply can't track it |
| `HOMEWORK_16_random_jitter_not_a_tone` | Elevated BER that's genuinely just broadband random jitter - confirming there's *no* periodic aggressor to chase |
| `HOMEWORK_17_ddr_wrong_chip_targeted` | DDR: controller's Target chip and the only populated chip's Chip number simply disagree - a pure CS strapping/addressing mismatch |
| `HOMEWORK_18_ddr_vref_not_retrained` | DDR: Vref trained on a near-ideal bench fixture, never retrained for the real board's lossier trace |
| `HOMEWORK_19_ddr_duplicate_chip_select` | DDR: two populated chips both strapped as chip 0 - a board-assembly mistake with the same symptom as #17 but a different root cause |
| `HOMEWORK_20_ddr_zq_cal_failed` | DDR: ZQ calibration against the 240 ohm reference never landed cleanly - a real termination mismatch and reflection, not a bad setting |
| `HOMEWORK_21_ddr_odt_misconfigured` | DDR: ZQ cal is clean, but the ODT mode-register target itself is wrong - the same symptom as #20, a different real cause |
| `HOMEWORK_22_ddr_cs_training_missing` | DDR: CS/CA training never ran - this chip's own chip-select sampling lags the real signal, missing short commands like reads |
| `HOMEWORK_23_ddr_write_leveling_missing` | DDR: write levelling never ran - all eight DQ bits skewed equally, corrupting almost every word uniformly |
| `HOMEWORK_24_ddr_per_bit_deskew_incomplete` | DDR: per-bit write-DQ deskew left incomplete on two pins only - a sparser, data-pattern-dependent failure, contrasted with #23 |
| `HOMEWORK_25_ddr_dfe_disabled` | DDR: the same ODT problem as #21, fixed a different way - the chip's own DQ receiver DFE is simply left off |

Presets 9-13 use fuller, more realistic chains (every stage that's actually relevant to the fault's domain
is present and switched on - if the problem is on the receiver, the Receiver stage is there, not modelled
as a side-effect of an earlier stage's parameter) and were added after a design note: an earlier version of
09/05 modelled an RX-side fault (termination, crosstalk feeding into a BER claim) without actually including
the Receiver stage. Both were corrected to include it.

Presets 14-16 need the **Jitter / Phase Noise Scope** specifically - their symptom doesn't separate from
"just noise" or "just a lossy channel" on the Eye Scope alone, only on a frequency-domain jitter measurement.
14 and 16 are a deliberate contrasting pair: the same "is there an EMI source?" question, one where the
spectrum says yes (a clear spike, read off its frequency) and one where it says no (a flat, noisy floor -
don't go chasing a tone that isn't there). 15 is a frequency-domain idea some players may not have met
before - a receiver's CDR jitter tolerance is frequency-dependent, so the *same* nominal jitter spec can
pass at one offset frequency and fail at another with no setting changed.

Presets 17-19 are the first homework scenarios on the **DDR** side (see "DDR stages" above) - no eye, no
BER, no jitter spectrum; the Homework Hint window's prompt/hint/explanation are the same, but what you
measure is the DDR Controller's live BIST word/error counters instead. 17 and 19 land on the identical
symptom (the targeted chip never answers a single word) from two different real causes - a simple
wrong-target mistake vs. two populated chips both left strapped to the same chip-select line - a
deliberate pair, the same idea as 14/16 on the jitter side: the same measured result can have more than
one real explanation, so the fix has to come from checking the actual cause, not just matching a symptom
to a familiar story. 18 is the Vref-training lesson (see "DDR stages" above) and needed the new **DDR
Lossy Channel** stage to be a genuine signal-level scenario rather than just a parameter mismatch.

Building 17-19 caught a real, and significant, pre-existing bug - not in the new DDR stages, in
`HostEngine::rebuildConnections()` itself, which every stage in every chain goes through: it hard-coded 2
channels for every stage-to-stage connection, which happens to be exactly right for a SerDes differential
pair but silently truncated a DDR chain's 18-channel bus down to just its first 2 (CK+/-) - every chip past
the first one in a chain would see clock activity and nothing else, with no error or warning. It was found
because homework 18 measured the exact same "100% of words checked, chip never selected" signature as 17
and 19 (`DdrChip::status.everSelected` stayed false even with Target chip and Chip number correctly
matching) - not the ~95-98% Vref-mismatch error rate it was written to demonstrate. That coincidence (a
Vref homework measuring identically to two unrelated chip-select homeworks) is exactly the kind of thing
this project's "verify every claim by live measurement, not inspection" discipline exists to catch. Fixed
by connecting as many channels as the narrower side of each connection actually has, instead of a fixed
count; confirmed by reloading the exact saved preset through the real `HostEngine` in isolation before and
after the fix (chip stayed silent before, answered correctly - and landed at 98.3% word errors - after),
then re-running the full measurement pass across all 19 presets to confirm no regression on the SerDes
side (whose stages are all 2-channel already, so `min(2, 2)` is unchanged) and that 17/19 still show the
right symptom for the right reason now.

Presets 20-25 map directly onto a real DDR/LPDDR5 validation/training sequence (ZQ calibration; CA/CS
training; write levelling; read gate/receive-enable training; read DQ training; write DQ training; Vref
training as a 2D sweep; DFE/equalization; duty-cycle training), and needed three new trainable physical
properties on DDR Chip that didn't exist before this pass - **ODT**, **ZQ cal error**, **CS timing error**,
plus turning the existing per-bit `dqskew0..7` parameters and a new **DFE** toggle into genuine signal-level
mechanisms (see "DDR stages" above for the Gamma/reflection math). 20/21 are a deliberate pair in the same
spirit as 17/19: identical ~98% word-error symptom, two different real causes (a calibration that missed
its target vs. a target that was simply programmed wrong). 23/24 are a deliberate contrast the other way -
the *same* kind of mistake (per-bit DQ skew) at two different scopes gives two visibly different failure
rates: write levelling (all eight bits, ~98% of words wrong) vs. incomplete per-bit deskew (two bits,
~11% of words wrong, and only on the addresses where those specific bits happen to toggle) - a real
verification-harness finding, not a chosen number: the default BIST pattern is "address as data", not
checkerboard, so only a fraction of consecutive addresses actually flip any one given bit. 25 reuses 21's
exact broken setup to make a different point - that a signal-integrity problem a board can't fix at its
physical source can sometimes still be absorbed by turning on equalization that was simply left disabled.
Of the nine validation-sequence steps, three are deliberately not represented as homeworks yet, for reasons
recorded in "DDR stages" above rather than silently skipped: read gate/receive-enable training and read DQ
training both need the Phase 2 electrical read loopback; duty-cycle training needs the real
double-pumped-per-clock timing upgrade; and Vref's 2D (Vref x timing) joint-training nature is already
covered, if not literally as a 2D sweep, by Homework 18. The full validation-sequence reference text this
pass was built from is kept in this project's memory (`ddr_training_procedures.md`), not duplicated here.

A `HostEngine::Homework` struct (title/prompt/hint/explanation) round-trips through `createChainXml`/
`loadChain` as an optional `<HOMEWORK>` element, so resaving a homework chain keeps its exercise attached.
There's no UI to author one - `Assets/generate_homework.cpp` (a one-off generator, not part of the app
build; its own header comment has the throwaway CMakeLists to build/run it) is how these nineteen were
written and measured live before being committed; several needed real parameter tuning (and, in one case,
a corrected sign-convention assumption, found by directly comparing measured eye height with and without
the "fix") to actually demonstrate the failure they claim, not just sound plausible. The three jitter-scope
scenarios needed more tuning iterations than most: an early attempt at #14 pushed the injected jitter so
hard the receiver never synced at all (overshooting "measurable but subtle" into "total failure"), and #16
initially - unintentionally - *did* trip a false positive on the dominant-tone detector, which turned out to
be a real robustness gap in `JitterAnalyzer` (too few Welch-averaged segments gave a noisy enough per-bin
estimate that the single highest of ~500 bins cleared the "stands out above the floor" threshold by chance
often enough to matter); fixed by averaging more segments and raising the threshold, verified against 40
different pure-random-jitter seeds (0 false positives after the fix, several before it) - though a live
regeneration pass after the DDR work below still tripped it once more, so treat #16 as "usually, not
guaranteed, shows no tone" until that gets tightened further.

## The stages as VST3 / AU plugins

Every stage is also built as a plugin, so a chain can be assembled in any DAW:

| Plugin | Code | Stage |
|---|---|---|
| DAW_LAB Signal Generator | Dl01 | generator (with **Line rate** and **Modulation** parameters) |
| DAW_LAB FFE | Dl02 | differential FFE |
| DAW_LAB Lossy Channel | Dl03 | loss channel |
| DAW_LAB Trace Channel | Dl04 | drawn traces, game, crosstalk, reflections |
| DAW_LAB S-Param Channel | Dl05 | Touchstone channel |
| DAW_LAB Noise Injector | Dl06 | noise |
| DAW_LAB Receiver | Dl07 | CTLE / DFE / CDR / slicer, bit comparison |
| DAW_LAB Eye Scope | Dl08 | eye scope |
| DAW_LAB Jitter Scope | Dl09 | TIE strip chart, histogram, jitter/phase-noise spectrum |

They are built by the same `cmake --build` and installed to `~/Library/Audio/Plug-Ins/VST3` and
`.../Components`. Use them as a chain of inserts on one stereo track (R = +, L = -), generator first.

- **Shared link.** The plugins are separate binaries, so the link between them (line rate, modulation, the
  probe the FFE trains against, the sent/received bit logs) lives in a small memory-mapped file for the
  host process, `~/Library/Caches/DAW_LAB/link/link-<pid>-<start>` (`Source/Builtin/SharedLink.h`). Stale files
  of dead processes are deleted on the way in. If it can't be mapped (a sandboxed host), a plugin falls back to a
  private link and works, but is not connected to the others.
- **Line rate and modulation** are parameters of the Signal Generator plugin (the DAW_LAB host has its own
  strip, and its built-in generator has no such parameters; the app's link is the same shared one, so plugins
  loaded into the app follow the strip).
- The generator is an *effect* that ignores its input; put it on a track that is being processed.
- Plugin state is the stage's own state (parameters, drawn board, embedded Touchstone file).

## Build

    cmake -S . -B build
    cmake --build build -j8
    open build/DawLab_artefacts/Release/DAW_LAB.app

`cmake --build build --target DawLab` builds only the app.

To reuse an already-fetched JUCE checkout instead of cloning:

    cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/juce-src

## Layout

- `Source/Main.cpp` - application and main window
- `Source/HostEngine.*` - audio/MIDI devices, format manager, known-plugin list, the processing graph (chain), preset save/load
- `Source/ChainView.*` - the visible signal-chain list
- `Source/Plugin/PluginEntry.cpp` - entry point compiled into each plugin (stage chosen by the target)
- `Source/Builtin/SharedLink.h` - the process-wide link
- `Source/MainComponent.*` - plugin picker, preset buttons, scan/audio-settings dialogs, editor windows

Settings (known plugins, audio device state) persist in
`~/Library/Application Support/DAW_LAB/`.
