#include "PllCore.h"
#include "PllCoreEditor.h"

PllCore::PllCore (std::shared_ptr<PllLinkSettings> linkSettings)
    : BuiltInProcessorBase (id, "PLL Core", std::move (linkSettings), createLayout(), numChannels)
{
    random.setSeed (12345);   // repeatable, same convention as SignalGenerator's jitter seed
}

juce::AudioProcessorValueTreeState::ParameterLayout PllCore::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using P = juce::ParameterID;

    // DUT params - real registers/properties a PLL chip or bench synthesizer actually has.
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { "reffreq", 1 }, "Reference frequency (MHz)",
                                                              juce::NormalisableRange<float> (1.0f, 100.0f), 25.0f));
    layout.add (std::make_unique<juce::AudioParameterInt>   (P { "fbdivider", 1 }, "Feedback divider N", 2, 512, 40));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { "vcofree", 1 }, "VCO free-run frequency (MHz)",
                                                              juce::NormalisableRange<float> (50.0f, 5000.0f), 1000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { "loopkp", 1 }, "Loop filter proportional gain (R1 path)",
                                                              juce::NormalisableRange<float> (0.0f, 3000.0f), 290.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { "loopki", 1 }, "Loop filter integral gain (C1 path)",
                                                              juce::NormalisableRange<float> (0.0f, 20000.0f), 1100.0f));

    // SIM param - an impairment you inject to stress the loop, not a real PLL register.
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { "refjitter", 1 }, "Injected reference jitter (UI rms)",
                                                              juce::NormalisableRange<float> (0.0f, 0.2f), 0.0f));
    return layout;
}

void PllCore::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    refPhase = vcoPhase = fbPhase = 0.0;
    qa = qb = false;
    integral = 0.0;
    freqOffset = 0.0;
    cyclesInLock = 0;
    status.locked = false;
    status.refCycles = 0;
}

// Default loopkp/loopki were chosen (not guessed blind - see the scratch verification harness) to land
// near a textbook-style zeta ~ 0.7 with the loop bandwidth comfortably below the default reference
// frequency, using the closed-form this exact discrete model reduces to under small-phase-error averaging:
//   wn = sqrt(loopki / N),  zeta = loopkp / (2 * sqrt(N * loopki))
// (derived from: dPe/dt = -(2*pi/N) * (Kpd*Pe + x), dx/dt = Ki_c*Pe, with Kpd = loopkp/(2*pi),
//  Ki_c = loopki/(2*pi); the 1/sampleRate factor in the integral update below is what makes loopki's
//  meaning - and this formula - independent of the host's sample rate.)
void PllCore::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < numChannels)
        return;

    const double refFreqHz = juce::jlimit (1.0, 100.0, (double) getParam ("reffreq"));
    const int N = juce::jlimit (2, 512, (int) getParam ("fbdivider"));
    const double vcoFreeHz = juce::jlimit (50.0, 5000.0, (double) getParam ("vcofree"));
    const double loopkp = juce::jmax (0.0, (double) getParam ("loopkp"));
    const double loopki = juce::jmax (0.0, (double) getParam ("loopki"));
    const double refJitterRms = juce::jmax (0.0, (double) getParam ("refjitter"));

    for (int i = 0; i < numSamples; ++i)
    {
        // --- reference oscillator
        refPhase += refFreqHz / sampleRate;
        bool refRose = false;
        if (refPhase >= 1.0)
        {
            refPhase -= 1.0;
            refRose = true;
            status.refCycles.fetch_add (1);

            if (refJitterRms > 0.0)
            {
                // Box-Muller, same convention as SignalGenerator's RJ - applied once per edge (a
                // reference clock carries no data pattern to schedule edges against, so there is no
                // per-symbol edge scheduler to hook into here, unlike the SerDes generator). The shift
                // itself is clamped (same maxEdgeShiftUI bound SignalGenerator uses), not the resulting
                // phase: clamping the RESULT instead (tried first) silently erased negative excursions,
                // which biased the long-run reference rate upward - caught by comparing the long-run
                // averaged VCO frequency against the exact target with jitter injected, which should land
                // on the same frequency whether or not jitter is on, and didn't until this fix. Wrapping
                // the result with fmod instead of clamping it has the same problem one step further out:
                // once a single edge's shift is large enough to be a sizeable fraction of a cycle, wrapping
                // it turns "a bit early/late" into "essentially a new random phase", which also biases the
                // average rate - clamping the shift itself keeps it a bounded nudge, never a re-randomisation.
                const double u1 = juce::jmax (1.0e-12, (double) random.nextFloat()), u2 = random.nextDouble();
                double shift = refJitterRms * std::sqrt (-2.0 * std::log (u1))
                              * std::cos (juce::MathConstants<double>::twoPi * u2);
                shift = juce::jlimit (-0.45, 0.45, shift);
                refPhase += shift;   // refPhase is always a tiny residual (< one sample's worth) here, so
                                      // this cannot push it past +-0.45 of a full cycle either way - safe
                                      // without a further clamp or wrap.
            }
        }

        // --- VCO, driven by its free-run frequency plus the loop filter's correction from last sample
        const double vcoFreqHz = juce::jlimit (1.0, 20000.0, vcoFreeHz + freqOffset);
        vcoPhase += vcoFreqHz / sampleRate;
        if (vcoPhase >= 1.0) vcoPhase -= 1.0;

        // --- feedback divider (/N): its own phase accumulator driven at vcoFreq/N - exactly equivalent to
        // counting real VCO edges mod N for an ideal integer divider (no divider-reset duty-cycle
        // artefacts modelled, a disclosed simplification).
        const double fbFreqHz = vcoFreqHz / (double) N;
        fbPhase += fbFreqHz / sampleRate;
        bool fbRose = false;
        if (fbPhase >= 1.0) { fbPhase -= 1.0; fbRose = true; }

        // --- 2-flip-flop phase/frequency detector: real circuit, not a linear phase-error subtraction -
        // this is what gives genuine (not assumed) frequency-acquisition behaviour outside +-2pi.
        if (refRose) qa = true;
        if (fbRose)  qb = true;
        double pumpSign = 0.0;
        if (qa && ! qb)      pumpSign = 1.0;
        else if (qb && ! qa) pumpSign = -1.0;
        if (qa && qb) { qa = false; qb = false; }

        // --- loop filter: R1 (proportional, fires only in the same instants the pump does - it is in
        // series with the same current) + C1 (integral, the loop's frequency memory). The /sampleRate on
        // the integral path is a true continuous-time integration (dV = I*dt/C, dt = 1/sampleRate), which
        // is what keeps loopki's meaning (and the wn/zeta formulas above) independent of host sample rate.
        integral += loopki * pumpSign / sampleRate;
        const double proportional = loopkp * pumpSign;
        freqOffset = integral + proportional;

        // --- lock detector: a derived measurement for display only, not fed back into the loop. Wrapped
        // to +-0.5 cycle, so it is only meaningful once the loop is near lock (during acquisition it will
        // cycle rapidly through the wrap, which is expected, not a bug).
        double cycleDiff = refPhase - fbPhase;
        cycleDiff -= std::round (cycleDiff);
        const double phaseErrorRad = cycleDiff * juce::MathConstants<double>::twoPi;
        status.phaseErrorRad = (float) phaseErrorRad;

        if (refRose)
        {
            if (std::abs (phaseErrorRad) > unlockThresholdRad)
            {
                cyclesInLock = 0;
                status.locked = false;
            }
            else if (std::abs (phaseErrorRad) < lockThresholdRad)
            {
                if (++cyclesInLock >= lockWindowCycles)
                    status.locked = true;
            }
            else
            {
                cyclesInLock = 0;
            }
        }

        status.vcoFreqHz = (float) vcoFreqHz;
        status.controlSignal = (float) freqOffset;

        // Published for stages that cannot see PllCore directly (PllJitterScope, probing the bus a stage
        // later - see the long comment on PllLinkSettings). Only needs updating once per block really, but
        // doing it once per sample is harmless and avoids a second loop.
        link->refFreqHz = (float) refFreqHz;
        link->vcoFreqHz = (float) vcoFreqHz;
        link->fbDivider = N;
        link->locked = status.locked.load();

        buffer.setSample (refCh, i, (float) (refPhase < 0.5 ? 1.0 : -1.0));
        buffer.setSample (vcoCh, i, (float) (vcoPhase < 0.5 ? 1.0 : -1.0));
        buffer.setSample (fbCh, i, (float) (fbPhase < 0.5 ? 1.0 : -1.0));
        buffer.setSample (ctrlCh, i, (float) freqOffset);
    }
}

juce::AudioProcessorEditor* PllCore::createEditor()
{
    return new PllCoreEditor (*this);
}
