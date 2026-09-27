# DAW_LAB

A plugin host sandbox (JUCE 8.0.4, C++20). Scans AU/VST3 plugins and runs a serial
chain of them: device input -> plugin 1 -> ... -> plugin N -> device output (MIDI input
goes to every plugin that accepts it). The chain order is shown top to bottom in the
window; each row has Bypass / Edit / Up / Down / Remove. With an empty chain, input
passes straight to output.

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

The chain list starts with four built-in stages. They are ordinary chain members (bypass, reorder, presets all work),
and share one **line rate** and **NRZ / PAM4** setting, chosen in the main window. Real time is scaled by 1,000,000:
**1 GBd on the wire runs as 1 kBd here, and a 5 GHz Nyquist tone is 5 kHz.** Every signal is a differential pair:
**R = +, L = -**; the eye scope looks at R - L.

Typical chain: `Signal Generator -> FFE -> Lossy Channel -> Eye Scope` (put the FFE after the channel to model an RX FFE).

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
