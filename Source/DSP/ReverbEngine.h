#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include "Biquad.h"
#include "Common.h"

namespace onirique::reverb
{
/*  ONIRIQUE VERB : la reverb de la page FX de VOCALBOX, devenue un plugin à part entière, en insert, sans latence.

    - Réseau de 16 lignes à retards modulés (FDN, matrice de Hadamard), diffusion d'entrée par 6 passe-tout courts
      et un passe-tout dans chaque boucle (la queue est dense dès les premières millisecondes : pas de grain sur les
      consonnes), amortissement en trois bandes calculé pour le temps de DECAY (méthode de Jot : les graves
      s'éteignent plus tôt selon l'espace, les aigus à leur temps), réflexions précoces pour ROOM et HALL,
      octave réinjectée pour SHIMMER.
    - Entrée stéréo : chaque côté entre dans la moitié du réseau, une source à gauche le reste au début de la queue.
      Une voix mono donne exactement le même réseau des deux côtés.
    - Modulation : une sinusoïde et une dérive lente au hasard par ligne, pour une queue qui vit sans chorus marqué.
    - Modes : ROOM, PLATE I (brillante, dense), PLATE II (plus sombre et plus large), HALL, SHIMMER, et ONIRIQUE :
      une salle immense et sombre, modulée en profondeur, avec un voile d'octave.
    - Changer SIZE fait glisser la longueur des lignes (comme une bande qui ralentit) : aucun clic. Changer de mode
      fond le niveau, les réflexions et l'octave : aucun saut.
    - INFINITE : le réseau devient sans perte et ne reçoit plus rien, la queue tient indéfiniment.
    - EQ IN (ce qui entre dans la reverb) et EQ OUT (la queue qui en sort) : coupe-bas, grave, médium, aigu,
      coupe-haut. Une bande neutre est sautée : à zéro, l'EQ IN ne touche à rien.
    - Le MIX de la queue : largeur stéréo (M/S), DUCKING (la reverb s'efface sous la voix),
      CLEAN (sifflantes retirées avant la reverb, bas-médiums creusés dans la queue, ancienne queue effacée quand
      une nouvelle phrase commence), PULSE (la queue respire au tempo du projet).
    - STUTTER, au tempo : CHOP (la queue hachée net), REPEAT (le premier morceau de chaque groupe de quatre est
      rejoué trois fois), RAMP (les coupes accélèrent à chaque temps de la mesure). Bords adoucis en 2 ms : aucun clic.
    - MIX : la voix sèche reste à 100 % jusqu'à 50 %, puis s'efface ; la reverb monte jusqu'à 100 % à 50 %.
*/

// tampon circulaire, lecture fractionnaire (Hermite 4 points)
class ModDelay
{
public:
    void prepare (int maxSamples)
    {
        int s = 1;
        while (s < maxSamples + 8) s <<= 1;
        buf.assign ((size_t) s, 0.0f);
        mask = s - 1;
        w = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // valeur écrite il y a d échantillons (d >= 2)
    float read (float d) const noexcept
    {
        d = juce::jlimit (2.0f, (float) (mask - 4), d);
        const int di = (int) d;                      // d > 0 : la troncature suffit, sans floor()
        const int i = w - 2 - di;
        const float f = 1.0f - (d - (float) di);     // f = 1 retombe exactement sur l'échantillon suivant
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
    float tap (int d) const noexcept { return at (w - 1 - d); }
    // lecture linéaire, pour un retard qui ne fait que glisser (exacte une fois arrivé sur un nombre entier)
    float readLinear (float d) const noexcept
    {
        d = juce::jlimit (1.0f, (float) (mask - 2), d);
        const int di = (int) d;
        const float f = d - (float) di, a = at (w - 1 - di);
        return a + f * (at (w - 2 - di) - a);
    }

private:
    float at (int i) const noexcept { return buf[(size_t) (i & mask)]; }
    std::vector<float> buf;
    int mask = 0, w = 0;
};

inline float coefFor (double seconds, double fs) noexcept { return (float) (1.0 - std::exp (-1.0 / (seconds * fs))); }

/** Un EQ de cinq bandes : coupe-bas, grave (plateau), médium (cloche), aigu (plateau), coupe-haut.
    La même définition sert au moteur et à la courbe de l'interface. */
struct Eq
{
    static constexpr int kBands = 5;
    float lowCut = 20.0f, highCut = 20000.0f, lowFreq = 200.0f, lowGain = 0.0f, midFreq = 1000.0f, midGain = 0.0f,
          highFreq = 6000.0f, highGain = 0.0f;

    bool operator== (const Eq& o) const noexcept
    {
        return lowCut == o.lowCut && highCut == o.highCut && lowFreq == o.lowFreq && lowGain == o.lowGain
            && midFreq == o.midFreq && midGain == o.midGain && highFreq == o.highFreq && highGain == o.highGain;
    }
    bool operator!= (const Eq& o) const noexcept { return ! (*this == o); }

    // les cinq filtres ; on[b] = false quand la bande ne change rien (elle est alors sautée)
    void design (double fs, BiquadCoeffs (&c)[kBands], bool (&on)[kBands]) const noexcept
    {
        c[0] = BiquadCoeffs::highpass (fs, juce::jlimit (20.0f, 1000.0f, lowCut), 0.707);
        c[1] = BiquadCoeffs::lowShelf (fs, lowFreq, 0.707, lowGain);
        c[2] = BiquadCoeffs::peaking (fs, midFreq, 0.9, midGain);
        c[3] = BiquadCoeffs::highShelf (fs, highFreq, 0.707, highGain);
        c[4] = BiquadCoeffs::lowpass (fs, std::min ((double) juce::jlimit (1000.0f, 20000.0f, highCut), 0.45 * fs), 0.707);
        on[0] = lowCut > 20.5f;
        on[1] = std::abs (lowGain) > 0.05f;
        on[2] = std::abs (midGain) > 0.05f;
        on[3] = std::abs (highGain) > 0.05f;
        on[4] = highCut < 19990.0f;
    }
};

enum Mode { room = 0, plate1, plate2, hall, shimmer, dream, numModes };

struct Params
{
    int mode = plate1;
    float mix = 0.3f, preDelayMs = 20.0f, decay = 2.5f, size = 0.6f, diffusion = 0.8f, modRate = 0.6f, modDepth = 0.3f;
    float width = 1.0f, ducking = 0.0f, clean = 0.0f, pulse = 0.0f;
    Eq eqIn, eqOut = [] { Eq e; e.lowCut = 150.0f; e.highCut = 9000.0f; return e; }();
    double pulseBeats = 0.5;        // durée d'une respiration de PULSE, en noires
    int stutterMode = 0;            // 0 OFF, 1 CHOP, 2 REPEAT, 3 RAMP
    double stutterBeats = 0.25;     // durée d'une découpe, en noires
    float stutterMix = 1.0f;
    bool freeze = false, bypass = false;
    float outputDb = 0.0f;
};

class Engine
{
public:
    static constexpr int kLines = 16;
    static constexpr int kHistory = 512;   // niveaux gardés pour l'interface (un par tranche de 256 échantillons)
    static constexpr int kScope = 4096;    // derniers échantillons envoyés / de la queue, pour le spectre de l'EQ

    /** Gain de découpe de CHOP et RAMP à la position `beats` (en noires) ; `ramp` : durée des bords, en noires. */
    static float stutterGate (int mode, double beats, double period, double ramp) noexcept
    {
        if (mode == 3)   // RAMP : à chaque temps de la mesure, des coupes deux fois plus serrées
        {
            const double inBar = std::fmod (beats, 4.0);
            period = std::max (1.0 / 64.0, period / std::pow (2.0, std::floor (inBar)));
            beats = inBar;
        }
        const double ph = std::fmod (beats, period), half = 0.5 * period;
        if (ph >= half) return 0.0f;
        return (float) juce::jlimit (0.0, 1.0, std::min (ph, half - ph) / std::max (1.0e-6, ramp));
    }

    void prepare (double sampleRate, const Params& p)
    {
        fs = sampleRate;
        for (auto& l : lines) l.prepare ((int) (0.30 * fs));
        for (auto& l : loopAp) l.prepare ((int) (0.12 * fs));
        for (auto& d : preDelay) d.prepare ((int) (1.6 * fs));    // jusqu'à une noire à 40 BPM (pre-delay au tempo)
        for (auto& r : repeatBuf) r.assign ((size_t) (1.6 * fs), 0.0f);
        // diffusion d'entrée : six passe-tout courts (longueurs à 29 761 Hz) ; courts, ils rendent l'attaque dense tout de suite
        static constexpr int apLen[kAp] { 71, 53, 191, 139, 107, 83 };
        for (int a = 0; a < kAp; ++a)
        {
            apD[a] = (int) (apLen[a] * fs / 29761.0);
            for (auto& side : ap) side[a].prepare (apD[a] + 4);
        }
        for (auto& l : erLine) l.prepare ((int) (0.12 * fs));
        shimBuf.prepare ((int) (0.2 * fs));
        erLp.setCoeffs (BiquadCoeffs::lowpass (fs, 6500.0, 0.707));
        shimLp.setCoeffs (BiquadCoeffs::lowpass (fs, std::min (7000.0, 0.45 * fs), 0.707));
        shimHp.setCoeffs (BiquadCoeffs::highpass (fs, 300.0, 0.707));
        sibHp.setCoeffs (BiquadCoeffs::highpass (fs, std::min (5000.0, 0.4 * fs), 0.707));
        for (int k = 0; k < kLines; ++k)
        {
            lfoC[k] = std::cos ((float) k * 0.73f);
            lfoS[k] = std::sin ((float) k * 0.73f);
            wanderA[k] = wanderB[k] = wanderT[k] = 0.0f;
        }
        rng = 0x9e3779b9u;
        wanderLeft = 0;
        wanderLine = 0;

        duckAtk = coefFor (0.010, fs); duckRel = coefFor (0.250, fs);
        sibAtk = coefFor (0.001, fs);  sibRel = coefFor (0.060, fs);
        onFast = coefFor (0.005, fs);  onFastRel = coefFor (0.040, fs); onSlow = coefFor (0.300, fs);
        clearRel = coefFor (0.350, fs);
        gainSmooth = coefFor (0.015, fs);
        modeSmooth = coefFor (0.060, fs);
        preS.reset (fs, 0.1);

        prm = p;
        clearState();
        update();
        for (int k = 0; k < kLines; ++k) { lenNow[k] = lineLen[k]; apNow[k] = apLenT[k]; }
        outGainNow = outGain; erGainNow = erGain; shimNow = shimmerAmt;
        preS.setCurrentAndTargetValue (preSamples());
        const auto g = mixGains (p);
        dryG = g[0]; wetG = g[1];
        inG = p.freeze ? 0.0f : 1.0f;
        outG = p.bypass ? 1.0f : dbToGain (p.outputDb);
        freePhase = 0.0;
    }

    void set (const Params& p) noexcept
    {
        const bool dirty = p.mode != prm.mode || p.decay != prm.decay || p.size != prm.size || p.diffusion != prm.diffusion
                        || p.eqIn != prm.eqIn || p.eqOut != prm.eqOut || p.clean != prm.clean || p.freeze != prm.freeze;
        prm = p;
        if (dirty) update();
        preS.setTargetValue (preSamples());
    }

    /** Traite le bloc sur place. ppq : position de lecture en noires (< 0 si le DAW ne joue pas), bpm : tempo. */
    void process (float* const* ch, int numCh, int n, double bpm, double ppq) noexcept
    {
        const auto mg = mixGains (prm);
        const float dryT = prm.bypass ? 1.0f : mg[0], wetT = prm.bypass ? 0.0f : mg[1];
        const float outT = prm.bypass ? 1.0f : dbToGain (prm.outputDb);
        const float inT = prm.freeze ? 0.0f : 1.0f;
        const float apG[kAp] { 0.75f * diffAmt, 0.75f * diffAmt, 0.625f * diffAmt, 0.625f * diffAmt, 0.6f * diffAmt, 0.6f * diffAmt };
        const float loopG = 0.6f * diffAmt;    // passe-tout dans les boucles : la densité suit DIFFUSION
        const float modSamples = (0.0003f + 0.0012f * prm.modDepth) * modMul * (float) fs;
        const float sideG = prm.width;
        const double beatsPerSample = std::max (20.0, bpm) / (60.0 * fs);
        const double period = std::max (1.0 / 32.0, prm.pulseBeats);
        const float lenGlide = coefFor (0.12, fs);
        const double stPeriod = std::max (1.0 / 64.0, prm.stutterBeats);
        const double stRamp = 0.002 / 60.0 * std::max (20.0, bpm);                         // 2 ms, en noires
        const int stSlice = std::min ((int) repeatBuf[0].size() - 1, (int) (stPeriod / beatsPerSample));
        const int stRampS = std::max (1, (int) (0.002 * fs));

        // modulation : une rotation par échantillon (sinusoïde sans sin()), et la dérive au hasard
        float rotC[kLines], rotS[kLines];
        for (int k = 0; k < kLines; ++k)
        {
            const double inc = 2.0 * juce::MathConstants<double>::pi * prm.modRate * lfoMul[k] / fs;
            rotC[k] = (float) std::cos (inc);
            rotS[k] = (float) std::sin (inc);
        }
        const float wanderK = coefFor (1.0 / (2.0 * juce::MathConstants<double>::pi * 0.8 * prm.modRate), fs);
        const int wanderEvery = std::max (4, (int) (fs / (1.3 * prm.modRate)) / kLines);   // chaque ligne : une fois par période

        for (int i = 0; i < n; ++i)
        {
            const float xl = ch[0][i], xr = numCh > 1 ? ch[1][i] : xl;
            const float in = 0.5f * (xl + xr);

            // ---- CLEAN, avant la reverb : les sifflantes ne partent pas dans la queue
            float sl = xl, sr = xr;
            if (prm.clean > 0.001f)
            {
                const float hl = sibHp.process (xl, 0), hr = sibHp.process (xr, 1);
                const float ah = std::abs (0.5f * (hl + hr)), af = std::abs (in);
                sibEnvH += (ah > sibEnvH ? sibAtk : sibRel) * (ah - sibEnvH);
                sibEnvF += (af > sibEnvF ? sibAtk : sibRel) * (af - sibEnvF);
                const float ratio = sibEnvH / (sibEnvF + 1.0e-6f);
                const float cut = prm.clean * juce::jlimit (0.0f, 1.0f, (ratio - 0.3f) * 3.0f);
                sibCut += gainSmooth * (cut - sibCut);
                sl = xl - 0.9f * sibCut * hl;
                sr = xr - 0.9f * sibCut * hr;
            }

            for (int b = 0; b < Eq::kBands; ++b)   // EQ IN : ce qui entre dans la reverb
                if (inOn[b]) { sl = inF[b].process (sl, 0); sr = inF[b].process (sr, 1); }

            inG += gainSmooth * (inT - inG);
            preDelay[0].push (sl * inG);
            preDelay[1].push (sr * inG);
            const float pre = preS.getNextValue();
            float x[2] { preDelay[0].read (pre), preDelay[1].read (pre) };

            // réflexions précoces (ROOM, HALL) : chaque côté surtout de son côté, un peu de l'autre
            erLine[0].push (0.75f * x[0] + 0.25f * x[1]);
            erLine[1].push (0.25f * x[0] + 0.75f * x[1]);
            erGainNow += modeSmooth * (erGain - erGainNow);
            float erL = 0.0f, erR = 0.0f;
            if (erGainNow > 1.0e-4f)
            {
                for (int t = 0; t < kTaps; t += 2)
                {
                    erL += erLine[0].tap (erTap[t]) * erAmp[t];
                    erR += erLine[1].tap (erTap[t + 1]) * erAmp[t + 1];
                }
                erL = erLp.process (erL, 0) * erGainNow;
                erR = erLp.process (erR, 1) * erGainNow;
            }

            // diffusion d'entrée : 6 passe-tout en série, de chaque côté
            for (int s = 0; s < 2; ++s)
                for (int a = 0; a < kAp; ++a)
                {
                    const float d = ap[s][a].tap (apD[a] - 1);
                    const float v = x[s] + apG[a] * d;
                    ap[s][a].push (v);
                    x[s] = d - apG[a] * v;
                }

            // octave réinjectée (SHIMMER, ONIRIQUE) : décaleur de hauteur à deux têtes, fenêtres en sinus
            shimNow += modeSmooth * (shimmerAmt - shimNow);
            float shim = 0.0f;
            if (shimNow > 1.0e-4f)
            {
                shimPhase += 1.0f / shimWin;
                if (shimPhase >= 1.0f) shimPhase -= 1.0f;
                const float pA = shimPhase, pB = shimPhase < 0.5f ? shimPhase + 0.5f : shimPhase - 0.5f;
                const float a = shimBuf.read (2.0f + (1.0f - pA) * shimWin), b = shimBuf.read (2.0f + (1.0f - pB) * shimWin);
                shim = a * sinPi (pA) + b * sinPi (pB);
                shim = shimLp.process (shimHp.process (shim, 0), 0) * shimNow * inG;   // INFINITE : plus rien n'entre
            }

            // réseau de 16 lignes : lecture modulée, passe-tout, amortissement (graves / médiums / aigus), Hadamard
            float rd[kLines], y[kLines];
            for (int k = 0; k < kLines; ++k)   // glissements et modulation, sans branchement (vectorisé)
            {
                lenNow[k] += lenGlide * (lineLen[k] - lenNow[k]);
                apNow[k] += lenGlide * (apLenT[k] - apNow[k]);
                const float c = lfoC[k] * rotC[k] - lfoS[k] * rotS[k];
                lfoS[k] = lfoS[k] * rotC[k] + lfoC[k] * rotS[k];
                lfoC[k] = c;
                wanderA[k] += wanderK * (wanderT[k] - wanderA[k]);
                wanderB[k] += wanderK * (wanderA[k] - wanderB[k]);
                rd[k] = lenNow[k] + modSamples * (0.7f * lfoS[k] + 0.6f * wanderB[k]);
            }
            if (--wanderLeft <= 0)   // une ligne à la fois reçoit sa nouvelle direction
            {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                wanderT[wanderLine] = (float) (rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
                wanderLine = (wanderLine + 1) % kLines;
                wanderLeft = wanderEvery;
            }
            for (int k = 0; k < kLines; ++k)
            {
                const float v = lines[k].read (rd[k]);
                const float d = loopAp[k].readLinear (apNow[k]);
                const float w = v + loopG * d;
                loopAp[k].push (w);
                y[k] = d - loopG * w;
            }
            for (int k = 0; k < kLines; ++k)   // amortissement (vectorisé)
            {
                lpState[k] = absB[k] * y[k] + absA[k] * lpState[k];
                const float s = shB0[k] * lpState[k] + shB1[k] * shX[k] - shA1[k] * shY[k];
                shX[k] = lpState[k];
                shY[k] = s;
                y[k] = s;
            }
            float h[kLines];
            std::copy (y, y + kLines, h);
            for (int len = 1; len < kLines; len <<= 1)            // Hadamard rapide (normalisée par 1/4)
                for (int s = 0; s < kLines; s += len << 1)
                    for (int j = s; j < s + len; ++j)
                    {
                        const float a = h[j], b = h[j + len];
                        h[j] = a + b; h[j + len] = a - b;
                    }
            const float inj[2] { (x[0] + shim) * 0.35f, (x[1] + shim) * 0.35f };   // lignes paires : gauche, impaires : droite
            for (int k = 0; k < kLines; ++k)
                lines[k].push (0.25f * h[k] + inj[k & 1] * inSign[k]);

            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < kLines; ++k) { wl += y[k] * outL[k]; wr += y[k] * outR[k]; }
            outGainNow += modeSmooth * (outGain - outGainNow);
            wl *= outGainNow; wr *= outGainNow;
            if (shimNow > 1.0e-4f) shimBuf.push (0.5f * (wl + wr));

            // ---- le MIX de la queue : coupe-bas / coupe-haut, bas-médiums (CLEAN), largeur
            wl += erL;
            wr += erR;
            for (int b = 0; b < Eq::kBands; ++b)   // EQ OUT : la queue
                if (outOn[b]) { wl = outF[b].process (wl, 0); wr = outF[b].process (wr, 1); }
            wl = mud.process (wl, 0);
            wr = mud.process (wr, 1);
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * sideG;
            wl = sanitize (mid + side);
            wr = sanitize (mid - side);

            // spectre de l'EQ : ce qui entre (après l'EQ IN) et la queue (après l'EQ OUT)
            const int sp = scopePos.load (std::memory_order_relaxed);
            scopeIn[(size_t) sp].store (0.5f * (sl + sr), std::memory_order_relaxed);
            scopeOut[(size_t) sp].store (mid, std::memory_order_relaxed);
            scopePos.store ((sp + 1) & (kScope - 1), std::memory_order_release);

            // DUCKING : la reverb s'efface sous la voix (jusqu'à -24 dB au-dessus de -40 dBFS)
            const float a = std::abs (in);
            duckEnv += (a > duckEnv ? duckAtk : duckRel) * (a - duckEnv);
            float target = 1.0f;
            if (prm.ducking > 0.001f)
                target = dbToGain (-std::min (24.0f * prm.ducking, 0.6f * std::max (0.0f, gainToDb (duckEnv + 1.0e-9f) + 40.0f)));

            // CLEAN : une nouvelle phrase efface la queue de la précédente
            envFast += (a > envFast ? onFast : onFastRel) * (a - envFast);
            envSlow += onSlow * (a - envSlow);
            if (envFast > 0.01f && envFast > 2.5f * envSlow) clearEnv = 1.0f;
            else clearEnv -= clearRel * clearEnv;
            target *= 1.0f - 0.75f * prm.clean * clearEnv;

            // PULSE : la queue respire au tempo (calée sur la mesure quand le DAW joue)
            const double beats = ppq >= 0.0 ? ppq + (double) i * beatsPerSample : freePhase;
            if (prm.pulse > 0.001f)
            {
                const float phase = (float) std::fmod (beats / period, 1.0);
                const float shape = std::pow (0.5f + 0.5f * std::cos (6.2831853f * phase), 1.6f);
                target *= 1.0f - prm.pulse * (1.0f - shape);
            }
            freePhase += beatsPerSample;
            duckG += gainSmooth * (target - duckG);

            wl *= duckG; wr *= duckG;

            // STUTTER : la queue découpée au tempo
            if (prm.stutterMode > 0 && prm.stutterMix > 0.001f)
            {
                float tl, tr;
                if (prm.stutterMode == 2)   // REPEAT : groupe de quatre découpes, la première enregistrée, rejouée trois fois
                {
                    const int slice = (int) (std::fmod (beats, 4.0 * stPeriod) / stPeriod);
                    if (slice != stLastSlice) { stLastSlice = slice; stCount = 0; }   // compté à l'échantillon : relu exactement où il a été écrit
                    const int at = std::min (stCount++, stSlice);
                    if (slice == 0) { repeatBuf[0][(size_t) at] = wl; repeatBuf[1][(size_t) at] = wr; tl = wl; tr = wr; }
                    else { tl = repeatBuf[0][(size_t) at]; tr = repeatBuf[1][(size_t) at]; }
                    const float edge = juce::jlimit (0.0f, 1.0f, (float) std::min (at, stSlice - at) / (float) stRampS);
                    tl *= edge; tr *= edge;
                }
                else
                {
                    const float gate = stutterGate (prm.stutterMode, beats, stPeriod, stRamp);
                    tl = wl * gate; tr = wr * gate;
                }
                wl += prm.stutterMix * (tl - wl);
                wr += prm.stutterMix * (tr - wr);
            }
            dryG += gainSmooth * (dryT - dryG);
            wetG += gainSmooth * (wetT - wetG);
            outG += gainSmooth * (outT - outG);

            const float dl = xl * dryG, dr = xr * dryG;
            if (numCh > 1)
            {
                ch[0][i] = (dl + wl * wetG) * outG;
                ch[1][i] = (dr + wr * wetG) * outG;
            }
            else ch[0][i] = (dl + 0.5f * (wl + wr) * wetG) * outG;

            // niveaux pour l'interface
            chunkDry = std::max (chunkDry, std::abs (in));
            chunkWet = std::max (chunkWet, 0.5f * (std::abs (wl) + std::abs (wr)) * wetG);
            chunkOut = std::max (chunkOut, std::max (std::abs (ch[0][i]), std::abs (ch[numCh > 1 ? 1 : 0][i])));
            if (++chunkCount >= 256)
            {
                const int w = writePos.load (std::memory_order_relaxed);
                dryHistory[(size_t) w].store (chunkDry, std::memory_order_relaxed);
                wetHistory[(size_t) w].store (chunkWet, std::memory_order_relaxed);
                outHistory[(size_t) w].store (chunkOut, std::memory_order_relaxed);
                writePos.store ((w + 1) % kHistory, std::memory_order_release);
                chunkDry = chunkWet = chunkOut = 0.0f;
                chunkCount = 0;
            }
        }
        for (int k = 0; k < kLines; ++k)   // la rotation garde son rayon
        {
            const float r = 1.0f / std::sqrt (lfoC[k] * lfoC[k] + lfoS[k] * lfoS[k]);
            lfoC[k] *= r; lfoS[k] *= r;
        }
        freePhase = std::fmod (freePhase, 64.0);
        beatNow.store (ppq >= 0.0 ? ppq + n * beatsPerSample : freePhase, std::memory_order_relaxed);
    }

    // ---- lus par l'interface (thread GUI)
    double getBeat() const noexcept { return beatNow.load (std::memory_order_relaxed); }   // position en noires (STUTTER, PULSE)
    int getWritePos() const noexcept { return writePos.load (std::memory_order_acquire); }
    float getDry (int i) const noexcept { return dryHistory[(size_t) ((i % kHistory + kHistory) % kHistory)].load (std::memory_order_relaxed); }
    float getWet (int i) const noexcept { return wetHistory[(size_t) ((i % kHistory + kHistory) % kHistory)].load (std::memory_order_relaxed); }
    float getOut (int i) const noexcept { return outHistory[(size_t) ((i % kHistory + kHistory) % kHistory)].load (std::memory_order_relaxed); }
    double getSampleRate() const noexcept { return fs; }
    /** Recopie les kScope derniers échantillons (entrée de la reverb ou queue), du plus ancien au plus récent. */
    void copyScope (bool tail, float* dest) const noexcept
    {
        const int end = scopePos.load (std::memory_order_acquire);
        const auto& src = tail ? scopeOut : scopeIn;
        for (int i = 0; i < kScope; ++i) dest[i] = src[(size_t) ((end + i) & (kScope - 1))].load (std::memory_order_relaxed);
    }

    /** Volumes sec / mouillé du MIX (0 à 1) : la voix reste entière jusqu'à 50 %, la reverb est pleine dès 50 %. */
    static std::array<float, 2> mixGains (const Params& p) noexcept
    {
        const float m = juce::jlimit (0.0f, 1.0f, p.mix);
        return { std::min (1.0f, 2.0f * (1.0f - m)), std::min (1.0f, 2.0f * m) };
    }

private:
    static constexpr int kTaps = 12, kAp = 6;

    // sin (pi x) pour x dans [0, 1] (Bhaskara, écart < 0,002)
    static float sinPi (float x) noexcept { const float p = x * (1.0f - x); return 16.0f * p / (5.0f - 4.0f * p); }

    // lfRatio : temps des graves (sous 320 Hz) par rapport à DECAY ; hfRatio : temps des aigus
    struct Character { float sizeMul, erGain, erSize, hfRatio, lfRatio, diff, shimmer, bright, mod; };
    static Character character (int mode) noexcept
    {
        switch (mode)
        {
            case room:    return { 0.45f, 0.55f, 0.55f, 0.45f, 0.80f, 0.80f, 0.00f,  0.00f, 0.6f };
            case plate2:  return { 0.95f, 0.00f, 0.00f, 0.50f, 0.70f, 1.00f, 0.00f, -0.30f, 1.2f };
            case hall:    return { 1.45f, 0.30f, 1.20f, 0.55f, 0.85f, 0.90f, 0.00f,  0.00f, 1.0f };
            case shimmer: return { 1.25f, 0.00f, 0.00f, 0.65f, 0.65f, 1.00f, 0.30f,  0.10f, 1.3f };
            case dream:   return { 1.70f, 0.00f, 0.00f, 0.42f, 0.90f, 1.00f, 0.14f, -0.20f, 3.0f };
            default:      return { 0.75f, 0.00f, 0.00f, 0.75f, 0.60f, 1.00f, 0.00f,  0.30f, 1.0f };   // PLATE I
        }
    }

    float preSamples() const noexcept { return std::max (2.0f, prm.preDelayMs * 0.001f * (float) fs); }

    void update() noexcept
    {
        const auto ch = character (prm.mode);
        static constexpr float baseMs[kLines] { 29.7f, 37.1f, 41.1f, 43.7f, 47.9f, 53.3f, 59.0f, 61.4f,
                                                67.3f, 71.1f, 73.9f, 79.6f, 83.5f, 89.2f, 97.3f, 101.6f };
        // chaque boucle garde sa longueur d'origine, partagée entre la ligne modulée et un passe-tout qui étale
        // chaque passage (part du passe-tout : rapports sans diviseur commun)
        static constexpr float apRatio[kLines] { 0.331f, 0.377f, 0.349f, 0.393f, 0.321f, 0.363f, 0.339f, 0.387f,
                                                 0.327f, 0.371f, 0.343f, 0.397f, 0.317f, 0.357f, 0.381f, 0.335f };
        const float sizeScale = ch.sizeMul * (0.45f + 1.1f * prm.size);
        const float rt = juce::jlimit (0.15f, 30.0f, prm.decay);
        const float rtHi = std::max (0.08f, rt * ch.hfRatio), rtLo = std::max (0.08f, rt * ch.lfRatio);
        // plateau des graves au premier ordre, réglé à 320 Hz (transformée bilinéaire, fréquence pré-déformée)
        const double wc = 2.0 * juce::MathConstants<double>::pi * 320.0;
        const double K = wc / std::tan (wc / (2.0 * fs));
        float gAvg = 0.0f;
        for (int k = 0; k < kLines; ++k)
        {
            const float total = std::min ((float) (0.29 * fs), baseMs[k] * sizeScale * 0.001f * (float) fs);
            apLenT[k] = std::max (3.0f, std::round (total * apRatio[k]));
            lineLen[k] = std::max (4.0f, total - apLenT[k]);
            const float loop = lineLen[k] + apLenT[k] + 2.0f;   // un tour de boucle, passe-tout compris
            // gain par tour pour que médiums, aigus (Nyquist) et graves décroissent de 60 dB en rt / rtHi / rtLo
            const float gm = std::pow (10.0f, -3.0f * loop / (rt * (float) fs));
            const float gh = std::pow (10.0f, -3.0f * loop / (rtHi * (float) fs));
            const float gl = std::pow (10.0f, -3.0f * loop / (rtLo * (float) fs));
            const float a = (gm - gh) / (gm + gh);
            absA[k] = prm.freeze ? 0.0f : a;                    // INFINITE : réseau sans perte
            absB[k] = prm.freeze ? 1.0f : gm * (1.0f - a);
            const double G = prm.freeze ? 1.0 : (double) (gl / gm), a0 = K + wc;
            shB0[k] = (float) ((K + G * wc) / a0);
            shB1[k] = (float) ((G * wc - K) / a0);
            shA1[k] = (float) ((wc - K) / a0);
            gAvg += gm;
            lfoMul[k] = 0.7f + 0.6f * (float) ((k * 7) % 11) / 10.0f;
            inSign[k] = (k * 5 + 3) % 4 < 2 ? 1.0f : -1.0f;
            // sorties gauche / droite : deux rangées de signes décorrélées, les lignes de chaque côté un peu plus fortes de leur côté
            const float lean = (k & 1) ? -kLean : kLean, norm = 0.25f / std::sqrt (1.0f + kLean * kLean);
            outL[k] = ((k & 1) ? -1.0f : 1.0f) * norm * (1.0f + lean);
            outR[k] = (((k >> 1) ^ (k >> 2)) & 1 ? -1.0f : 1.0f) * norm * (1.0f - lean);
        }
        gAvg /= (float) kLines;
        outGain = kLevel * std::pow (std::max (0.02f, 1.0f - gAvg * gAvg), 0.35f) * (1.0f + 0.25f * ch.bright);
        diffAmt = juce::jlimit (0.0f, 1.0f, prm.diffusion * ch.diff);
        erGain = ch.erGain;
        shimmerAmt = ch.shimmer;
        modMul = ch.mod;
        shimWin = (float) (0.06 * fs);
        static constexpr float erMs[kTaps] { 7.1f, 11.3f, 13.7f, 17.2f, 19.9f, 23.4f, 29.1f, 31.3f, 37.7f, 41.2f, 43.9f, 47.8f };
        for (int t = 0; t < kTaps; ++t)
        {
            erTap[t] = std::max (1, (int) (erMs[t] * std::max (0.2f, ch.erSize * (0.6f + 0.8f * prm.size)) * 0.001f * (float) fs));
            erTap[t] = std::min (erTap[t], (int) (0.115 * fs));
            erAmp[t] = 0.55f * std::pow (0.82f, (float) t);
        }
        auto setEq = [this] (const Eq& eq, Biquad (&f)[Eq::kBands], bool (&on)[Eq::kBands])
        {
            BiquadCoeffs c[Eq::kBands];
            bool now[Eq::kBands];
            eq.design (fs, c, now);
            for (int b = 0; b < Eq::kBands; ++b)
            {
                if (now[b] && ! on[b]) f[b].reset();   // bande qui se réveille : pas d'ancien état
                f[b].setCoeffs (c[b]);
                on[b] = now[b];
            }
        };
        setEq (prm.eqIn, inF, inOn);
        setEq (prm.eqOut, outF, outOn);
        mud.setCoeffs (BiquadCoeffs::peaking (fs, 350.0, 0.9, -6.0 * prm.clean));
    }

    void clearState() noexcept
    {
        for (auto& l : lines) l.reset();
        for (auto& l : loopAp) l.reset();
        for (auto& d : preDelay) d.reset();
        for (auto& l : erLine) l.reset();
        shimBuf.reset();
        for (auto& side : ap) for (auto& a : side) a.reset();
        for (int k = 0; k < kLines; ++k) lpState[k] = shX[k] = shY[k] = 0.0f;
        for (int b = 0; b < Eq::kBands; ++b) { inF[b].reset(); outF[b].reset(); }
        erLp.reset(); shimLp.reset(); shimHp.reset(); sibHp.reset(); mud.reset();
        duckEnv = envFast = envSlow = clearEnv = sibEnvH = sibEnvF = sibCut = 0.0f;
        duckG = 1.0f;
        shimPhase = 0.0f;
    }

    static constexpr float kLean = 0.35f;   // part de chaque côté gardée à la sortie (entrée stéréo)
    static constexpr float kLevel = 1.6f;   // niveau de la queue (celui de la version 1.0 sous un bruit continu)

    double fs = 48000.0, freePhase = 0.0;
    Params prm;
    ModDelay lines[kLines], loopAp[kLines], preDelay[2], ap[2][kAp], erLine[2], shimBuf;
    int apD[kAp] {};
    float lineLen[kLines] {}, lenNow[kLines] {}, apLenT[kLines] {}, apNow[kLines] {};
    float absA[kLines] {}, absB[kLines] {}, lpState[kLines] {}, shB0[kLines] {}, shB1[kLines] {}, shA1[kLines] {}, shX[kLines] {}, shY[kLines] {};
    float lfoMul[kLines] {}, inSign[kLines] {}, outL[kLines] {}, outR[kLines] {};
    float lfoC[kLines] {}, lfoS[kLines] {}, wanderA[kLines] {}, wanderB[kLines] {}, wanderT[kLines] {};
    int wanderLeft = 0, wanderLine = 0;
    std::uint32_t rng = 0x9e3779b9u;
    float outGain = 1.0f, diffAmt = 0.75f, erGain = 0.0f, shimmerAmt = 0.0f, shimWin = 2880.0f, shimPhase = 0.0f, modMul = 1.0f;
    float outGainNow = 1.0f, erGainNow = 0.0f, shimNow = 0.0f;
    bool inOn[Eq::kBands] {}, outOn[Eq::kBands] {};
    int erTap[kTaps] {};
    float erAmp[kTaps] {};
    Biquad inF[Eq::kBands], outF[Eq::kBands], erLp, shimLp, shimHp, sibHp, mud;
    juce::SmoothedValue<float> preS;

    float duckAtk = 0.01f, duckRel = 0.001f, sibAtk = 0.1f, sibRel = 0.001f, onFast = 0.01f, onFastRel = 0.001f, onSlow = 0.001f;
    float clearRel = 0.001f, gainSmooth = 0.01f, modeSmooth = 0.001f;
    float duckEnv = 0.0f, envFast = 0.0f, envSlow = 0.0f, clearEnv = 0.0f, sibEnvH = 0.0f, sibEnvF = 0.0f, sibCut = 0.0f;
    float duckG = 1.0f, dryG = 1.0f, wetG = 0.0f, outG = 1.0f, inG = 1.0f;

    float chunkDry = 0.0f, chunkWet = 0.0f, chunkOut = 0.0f;
    int chunkCount = 0;
    std::array<std::atomic<float>, kHistory> dryHistory {}, wetHistory {}, outHistory {};
    std::atomic<int> writePos { 0 };
    std::array<std::atomic<float>, kScope> scopeIn {}, scopeOut {};
    std::atomic<int> scopePos { 0 };
    std::atomic<double> beatNow { 0.0 };
    std::vector<float> repeatBuf[2];
    int stLastSlice = -1, stCount = 0;
};
} // namespace onirique::reverb
