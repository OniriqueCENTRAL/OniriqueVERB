// Banc d'essai du moteur de ONIRIQUE VERB. Chaque essai affiche sa mesure et OK / ECHEC ;
// le programme rend 1 si un essai échoue.
//
//   OniriqueReverbEngineTest [voix.wav [dossier de sortie]]
//   Avec un fichier : écrit la voix passée dans chaque mode (réglages du preset par défaut, MIX 35 %).

#include <juce_audio_formats/juce_audio_formats.h>
#include "DSP/ReverbEngine.h"

#include <complex>
#include <cstdio>

using namespace onirique::reverb;

namespace
{
constexpr double fs = 48000.0;
int failures = 0;

void check (bool ok, const char* what, double measured, const char* unit)
{
    std::printf ("%-58s %10.2f %-4s %s\n", what, measured, unit, ok ? "OK" : "ECHEC");
    if (! ok) ++failures;
}

// passe un signal mono (copié des deux côtés) dans le moteur ; sortie gauche + droite
std::vector<float> run (Engine& e, const std::vector<float>& in, std::vector<float>* right = nullptr,
                        std::function<void (Engine&, int)> perBlock = {}, double bpm = 120.0, bool playing = false)
{
    std::vector<float> L (in), R (in);
    double ppq = 0.0;
    for (int pos = 0, block = 0; pos < (int) in.size(); pos += 256, ++block)
    {
        if (perBlock) perBlock (e, block);
        const int n = std::min (256, (int) in.size() - pos);
        float* ch[2] { L.data() + pos, R.data() + pos };
        e.process (ch, 2, n, bpm, playing ? ppq : -1.0);
        ppq += n * bpm / (60.0 * fs);
    }
    if (right) *right = R;
    return L;
}

double rmsDb (const std::vector<float>& x, int from, int to)
{
    double s = 0.0;
    for (int i = std::max (0, from); i < std::min ((int) x.size(), to); ++i) s += (double) x[(size_t) i] * x[(size_t) i];
    return 10.0 * std::log10 (s / std::max (1, to - from) + 1.0e-20);
}

// temps de réverbération mesuré par intégration de Schroeder, droite de -5 à -25 dB (T20) extrapolée à -60 dB
double rt60 (const std::vector<float>& ir)
{
    std::vector<double> edc (ir.size());
    double acc = 0.0;
    for (int i = (int) ir.size() - 1; i >= 0; --i) { acc += (double) ir[(size_t) i] * ir[(size_t) i]; edc[(size_t) i] = acc; }
    const double total = edc[0];
    int t5 = -1, t25 = -1;
    for (int i = 0; i < (int) edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[(size_t) i] / total + 1.0e-30);
        if (t5 < 0 && db <= -5.0) t5 = i;
        if (t25 < 0 && db <= -25.0) { t25 = i; break; }
    }
    if (t5 < 0 || t25 < 0) return -1.0;
    return 3.0 * (double) (t25 - t5) / fs;
}

std::vector<float> noise (int n, float gain, int seed)
{
    juce::Random r (seed);
    std::vector<float> x ((size_t) n);
    for (auto& v : x) v = gain * (2.0f * r.nextFloat() - 1.0f);
    return x;
}

Params wetOnly (int mode, float decay)
{
    Params p;
    p.mode = mode; p.mix = 1.0f; p.decay = decay;
    p.eqOut = {};   // EQ OUT neutre
    return p;
}

// FFT radix 2 sur place (n puissance de 2)
void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto w = std::polar (1.0, -2.0 * 3.141592653589793 / (double) len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> v (1.0);
            for (size_t k = 0; k < len / 2; ++k, v *= w)
            {
                const auto x = a[i + k], y = a[i + k + len / 2] * v;
                a[i + k] = x + y; a[i + k + len / 2] = x - y;
            }
        }
    }
}

// Densité d'échos (Abel & Huang) : temps entre la première réflexion et l'instant où, sur 20 ms, la queue
// ressemble à un bruit (NED >= 0,9)
double densityTime (const std::vector<float>& ir)
{
    float peak = 0.0f;
    for (auto v : ir) peak = std::max (peak, std::abs (v));
    int onset = 0;
    while (onset < (int) ir.size() && std::abs (ir[(size_t) onset]) < 0.001f * peak) ++onset;
    const int win = (int) (0.020 * fs);
    for (int s = onset; s + win < (int) ir.size(); s += (int) (0.001 * fs))
    {
        double e = 0.0;
        for (int i = s; i < s + win; ++i) e += (double) ir[(size_t) i] * ir[(size_t) i];
        const double sigma = std::sqrt (e / win);
        int above = 0;
        for (int i = s; i < s + win; ++i) above += std::abs (ir[(size_t) i]) > sigma;
        if ((double) above / win / 0.3173 >= 0.9) return (double) (s + win / 2 - onset) / fs;
    }
    return -1.0;
}

// Coloration (sonnerie métallique) : écart type, de 200 Hz à 8 kHz, entre le spectre moyen de la queue
// et sa version lissée au tiers d'octave. Plus c'est haut, plus des résonances ressortent.
double coloration (const std::vector<float>& ir, int from, int to)
{
    constexpr int N = 4096;
    std::vector<double> avg (N / 2, 0.0);
    int frames = 0;
    for (int s = from; s + N <= to; s += N / 4, ++frames)
    {
        std::vector<std::complex<double>> a (N);
        double e = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * i / N);
            a[(size_t) i] = ir[(size_t) (s + i)] * w;
            e += std::norm (a[(size_t) i]);
        }
        fft (a);
        for (int k = 0; k < N / 2; ++k) avg[(size_t) k] += std::norm (a[(size_t) k]) / (e + 1.0e-30);
    }
    std::vector<double> db (N / 2);
    for (int k = 1; k < N / 2; ++k) db[(size_t) k] = 10.0 * std::log10 (avg[(size_t) k] / std::max (1, frames) + 1.0e-30);
    double sum = 0.0, sum2 = 0.0;
    int count = 0;
    for (int k = (int) (200.0 * N / fs); k < (int) (8000.0 * N / fs); ++k)
    {
        const int lo = std::max (1, (int) (k / 1.1225)), hi = std::min (N / 2 - 1, (int) (k * 1.1225) + 1);
        double sm = 0.0;
        for (int j = lo; j <= hi; ++j) sm += db[(size_t) j];
        const double d = db[(size_t) k] - sm / (hi - lo + 1);
        sum += d; sum2 += d * d; ++count;
    }
    const double mean = sum / count;
    return std::sqrt (sum2 / count - mean * mean);
}

double correlation (const std::vector<float>& a, const std::vector<float>& b, int from, int to)
{
    double xy = 0.0, xx = 0.0, yy = 0.0;
    for (int i = from; i < to; ++i) { xy += (double) a[(size_t) i] * b[(size_t) i]; xx += (double) a[(size_t) i] * a[(size_t) i]; yy += (double) b[(size_t) i] * b[(size_t) i]; }
    return xy / std::sqrt (xx * yy + 1.0e-30);
}

// temps de queue dans une bande (quatre passe-bande en série autour de hz : les bandes voisines ne s'y mêlent pas)
double bandRt60 (const std::vector<float>& ir, double hz)
{
    std::vector<float> x (ir);
    for (int stage = 0; stage < 4; ++stage)
    {
        onirique::Biquad bp;
        bp.setCoeffs (onirique::BiquadCoeffs::bandpass (fs, hz, 1.4));
        for (auto& v : x) v = bp.process (v, 0);
    }
    return rt60 (x);
}
} // namespace

int main (int argc, char** argv)
{
    Engine e;

    // 1. MIX 0 : la voix sort intacte
    {
        Params p; p.mix = 0.0f;
        e.prepare (fs, p);
        const auto in = noise ((int) fs, 0.5f, 1);
        const auto out = run (e, in);
        double err = 0.0;
        for (size_t i = 0; i < in.size(); ++i) err = std::max (err, (double) std::abs (out[i] - in[i]));
        check (err < 1.0e-6, "MIX 0 % : sortie = entree (ecart max)", err * 1.0e6, "e-6");
    }

    // 2. DECAY : temps de queue mesuré sur une impulsion, à 25 % près
    static const char* modeNames[] { "ROOM", "PLATE I", "PLATE II", "HALL", "SHIMMER", "ONIRIQUE" };
    for (int mode : { (int) room, (int) plate1, (int) plate2, (int) hall, (int) dream })
        for (float decay : { 1.0f, 2.5f, 6.0f })
        {
            auto p = wetOnly (mode, decay);
            p.modDepth = 0.0f;
            e.prepare (fs, p);
            std::vector<float> in ((size_t) (fs * (decay * 1.6 + 1.0)), 0.0f);
            in[100] = 1.0f;
            const double t = rt60 (run (e, in));
            char label[96];
            std::snprintf (label, sizeof (label), "DECAY %.1f s en %s : temps mesure", decay, modeNames[mode]);
            check (t > 0.75 * decay && t < 1.25 * decay, label, t, "s");
        }

    // 3. INFINITE : après 6 s sans voix, la queue n'a pas perdu plus de 3 dB
    {
        auto p = wetOnly (hall, 2.0f);
        e.prepare (fs, p);
        const auto in = [] { auto x = noise ((int) (fs * 8.0), 0.3f, 2); std::fill (x.begin() + (int) fs, x.end(), 0.0f); return x; }();
        const auto out = run (e, in, nullptr, [] (Engine& en, int block)
        {
            if (block == (int) (1.0 * fs / 256)) { auto q = wetOnly (hall, 2.0f); q.freeze = true; en.set (q); }
        });
        const double a = rmsDb (out, (int) (1.5 * fs), (int) (2.0 * fs)), b = rmsDb (out, (int) (7.5 * fs), (int) (8.0 * fs));
        check (std::abs (b - a) < 3.0, "INFINITE : variation de la queue en 6 s", b - a, "dB");
    }

    // 4. Stabilité : tous les modes au maximum (DECAY 20 s, SIZE et MOD à fond, largeur 200 %), 10 s de bruit fort
    for (int mode = 0; mode < numModes; ++mode)
    {
        auto p = wetOnly (mode, 20.0f);
        p.size = 1.0f; p.modDepth = 1.0f; p.modRate = 5.0f; p.width = 2.0f; p.diffusion = 1.0f;
        e.prepare (fs, p);
        std::vector<float> R;
        const auto L = run (e, noise ((int) (fs * 10.0), 0.7f, 3 + mode), &R);
        double peak = 0.0;
        bool finite = true;
        for (size_t i = 0; i < L.size(); ++i)
        {
            finite &= std::isfinite (L[i]) && std::isfinite (R[i]);
            peak = std::max (peak, (double) std::max (std::abs (L[i]), std::abs (R[i])));
        }
        char label[96];
        std::snprintf (label, sizeof (label), "Stabilite %s au maximum : crete de sortie", modeNames[mode]);
        check (finite && peak < 6.0, label, peak, "");
    }

    // 5. CLEAN : des sifflantes (bruit au-dessus de 5 kHz) entrent beaucoup moins dans la queue
    {
        auto sib = noise ((int) (fs * 3.0), 0.25f, 9);
        onirique::Biquad hp;
        hp.setCoeffs (onirique::BiquadCoeffs::highpass (fs, 6000.0, 0.707));
        for (size_t i = 0; i < sib.size(); ++i) sib[i] = ((i / 9600) % 3 == 0) ? hp.process (sib[i], 0) : 0.0f;   // « sss » de 200 ms
        double level[2];
        for (int c = 0; c < 2; ++c)
        {
            auto p = wetOnly (plate1, 2.0f);
            p.clean = (float) c;
            e.prepare (fs, p);
            const auto out = run (e, sib);
            level[c] = rmsDb (out, 0, (int) out.size());
        }
        check (level[0] - level[1] > 4.0, "CLEAN 100 % : sifflantes retirees de la queue", level[0] - level[1], "dB");
    }

    // 6. PULSE à la noire, 120 BPM, DAW en lecture : la queue respire (écart entre creux et bosses)
    {
        auto p = wetOnly (hall, 4.0f);
        p.pulse = 1.0f; p.pulseBeats = 1.0;
        e.prepare (fs, p);
        const auto out = run (e, noise ((int) (fs * 4.0), 0.2f, 11), nullptr, {}, 120.0, true);
        // fenêtres de 40 ms centrées sur les temps (bosses) et à mi-temps (creux)
        double on = 0.0, off = 0.0;
        for (int beat = 2; beat < 7; ++beat)
        {
            const int c = (int) (beat * 0.5 * fs);
            on += rmsDb (out, c - 960, c + 960);
            off += rmsDb (out, c + 12000 - 960, c + 12000 + 960);
        }
        check ((on - off) / 5.0 > 12.0, "PULSE 100 % : ecart bosse / creux", (on - off) / 5.0, "dB");
    }

    // 7. DUCKING : sous une voix forte, la reverb baisse
    {
        double level[2];
        for (int c = 0; c < 2; ++c)
        {
            auto p = wetOnly (plate1, 2.0f);
            p.mix = 0.5f;   // voix sèche et reverb à 100 % : on mesure la reverb seule en retirant la voix
            p.ducking = (float) c;
            e.prepare (fs, p);
            const auto in = noise ((int) (fs * 2.0), 0.3f, 5);
            const auto out = run (e, in);
            std::vector<float> wetPart (out.size());
            for (size_t i = 0; i < out.size(); ++i) wetPart[i] = out[i] - in[i];
            level[c] = rmsDb (wetPart, (int) fs, (int) (2.0 * fs));
        }
        check (level[0] - level[1] > 12.0, "DUCKING 100 % : reverb baissee sous la voix", level[0] - level[1], "dB");
    }

    // 8. STUTTER CHOP 1/16, 120 BPM, DAW en lecture : la queue est coupée net la moitié du temps
    {
        auto p = wetOnly (hall, 4.0f);
        p.stutterMode = 1; p.stutterBeats = 0.25;
        e.prepare (fs, p);
        const auto out = run (e, noise ((int) (fs * 4.0), 0.2f, 13), nullptr, {}, 120.0, true);
        const int slice = (int) (0.25 * 0.5 * fs);   // une double-croche à 120 BPM = 125 ms
        double open = 0.0, shut = 0.0;
        for (int k = 8; k < 24; ++k)
        {
            open += rmsDb (out, k * slice + slice / 8, k * slice + slice * 3 / 8);
            shut += rmsDb (out, k * slice + slice * 5 / 8, k * slice + slice * 7 / 8);
        }
        check ((open - shut) / 16.0 > 40.0, "STUTTER CHOP : ecart ouvert / coupe", (open - shut) / 16.0, "dB");
    }

    // 9. STUTTER REPEAT 1/8 : dans chaque groupe de quatre, les découpes 2 à 4 rejouent la première
    {
        auto p = wetOnly (hall, 4.0f);
        p.stutterMode = 2; p.stutterBeats = 0.5;
        e.prepare (fs, p);
        const auto out = run (e, noise ((int) (fs * 6.0), 0.2f, 17), nullptr, {}, 120.0, true);
        const int slice = (int) (0.5 * 0.5 * fs), group = 4 * slice;
        double worst = 1.0;
        for (int gStart = group; gStart + group <= (int) out.size(); gStart += group)
            for (int k = 1; k < 4; ++k)
            {
                double best = -1.0;   // les bords de découpe tombent à un échantillon près du calcul de l'essai
                for (int lag = -2; lag <= 2; ++lag)
                {
                    double xy = 0.0, xx = 0.0, yy = 0.0;
                    for (int i = 400; i < slice - 400; ++i)
                    {
                        const double a = out[(size_t) (gStart + i)], b = out[(size_t) (gStart + k * slice + i + lag)];
                        xy += a * b; xx += a * a; yy += b * b;
                    }
                    best = std::max (best, xy / std::sqrt (xx * yy + 1.0e-20));
                }
                worst = std::min (worst, best);
            }
        check (worst > 0.99, "STUTTER REPEAT : ressemblance des repetitions (pire)", worst, "");
    }

    // 10. STUTTER RAMP : au 4e temps, huit fois plus de coupes qu'au 1er
    {
        int cuts[4] {};
        float last = 0.0f;
        for (int i = 0; i < 400000; ++i)
        {
            const double t = 4.0 * i / 400000.0;
            const float gate = Engine::stutterGate (3, t, 0.5, 0.001);
            if (last > 0.5f && gate <= 0.5f) ++cuts[(int) t];
            last = gate;
        }
        check (cuts[3] == 8 * cuts[0], "STUTTER RAMP : coupes au 4e temps / au 1er", (double) cuts[3] / std::max (1, cuts[0]), "x");
    }

    // 11. EQ IN : un coupe-bas à 1 kHz empêche un son grave (100 Hz) d'entrer dans la reverb
    // 12. EQ OUT : +12 dB d'aigu rendent la queue d'un son aigu (8 kHz) plus forte
    {
        auto tone = [] (double hz)
        {
            std::vector<float> x ((size_t) (fs * 3.0));
            for (size_t i = 0; i < x.size(); ++i) x[i] = 0.3f * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) i / fs);
            return x;
        };
        double level[2];
        for (int c = 0; c < 2; ++c)
        {
            auto p = wetOnly (plate1, 2.0f);
            if (c == 1) p.eqIn.lowCut = 1000.0f;
            e.prepare (fs, p);
            const auto out = run (e, tone (100.0));
            level[c] = rmsDb (out, (int) fs, (int) (3.0 * fs));
        }
        check (level[0] - level[1] > 15.0, "EQ IN coupe-bas 1 kHz : grave retire de la reverb", level[0] - level[1], "dB");
        for (int c = 0; c < 2; ++c)
        {
            auto p = wetOnly (plate1, 2.0f);
            if (c == 1) p.eqOut.highGain = 12.0f;
            e.prepare (fs, p);
            const auto out = run (e, tone (8000.0));
            level[c] = rmsDb (out, (int) fs, (int) (3.0 * fs));
        }
        check (level[1] - level[0] > 9.0, "EQ OUT aigu +12 dB : queue aigue renforcee", level[1] - level[0], "dB");
    }

    // Mesures de la queue, mode par mode (impulsion, DECAY 2,5 s) ; niveau : la reverb seule sous un bruit continu
    std::printf ("\n%-10s %9s %9s %9s %9s %9s %9s %9s\n", "", "densite", "couleur", "corr L/R", "RT 125", "RT 1k", "RT 4k", "niveau");
    struct Verdict { const char* mode; bool dense, decayRight, bassShorter, smooth; };
    std::vector<Verdict> verdicts;
    for (int mode = 0; mode < numModes; ++mode)
    {
        auto p = wetOnly (mode, 2.5f);
        p.preDelayMs = 0.0f;
        // densité : moyenne sur cinq impulsions (la modulation n'est jamais au même endroit)
        double density = 0.0;
        for (int shot = 0; shot < 5; ++shot)
        {
            e.prepare (fs, p);
            std::vector<float> in ((size_t) (fs * 1.5), 0.0f), Rs;
            in[(size_t) (100 + shot * 4567)] = 1.0f;
            const auto Ls = run (e, in, &Rs);
            std::vector<float> Ms (Ls.size());
            for (size_t i = 0; i < Ls.size(); ++i) Ms[i] = 0.5f * (Ls[i] + Rs[i]);
            density += densityTime (Ms) / 5.0;
        }
        e.prepare (fs, p);
        std::vector<float> in ((size_t) (fs * 4.0), 0.0f), R;
        in[100] = 1.0f;
        const auto L = run (e, in, &R);
        std::vector<float> M (L.size());
        for (size_t i = 0; i < L.size(); ++i) M[i] = 0.5f * (L[i] + R[i]);
        e.prepare (fs, p);
        const auto loud = run (e, noise ((int) (fs * 3.0), 0.2f, 31));
        const double rtLo = bandRt60 (M, 125.0), rtMid = bandRt60 (M, 1000.0), color = coloration (M, (int) (0.15 * fs), (int) (1.4 * fs));
        std::printf ("%-10s %7.1f ms %6.2f dB %9.2f %7.2f s %7.2f s %7.2f s %6.1f dB\n", modeNames[mode], 1000.0 * density,
                     color, correlation (L, R, (int) (0.05 * fs), (int) (1.5 * fs)),
                     rtLo, rtMid, bandRt60 (M, 4000.0), rmsDb (loud, (int) fs, (int) (3.0 * fs)));
        verdicts.push_back ({ modeNames[mode], density < (mode == hall ? 0.1 : 0.06), std::abs (rtMid / 2.5 - 1.0) < 0.1,
                              rtLo < rtMid, color < 1.5 });
    }
    for (const auto& v : verdicts)
    {
        char label[96];
        std::snprintf (label, sizeof (label), "%s : dense, DECAY juste, graves courts, sans resonance", v.mode);
        check (v.dense && v.decayRight && v.bassShorter && v.smooth, label, 0.0, "");
    }

    // Entrée stéréo : une source à gauche seulement garde sa place (premiers instants, puis queue)
    {
        auto p = wetOnly (hall, 2.5f);
        p.preDelayMs = 0.0f;
        e.prepare (fs, p);
        auto L = noise ((int) (fs * 3.0), 0.3f, 21);
        std::fill (L.begin() + (int) fs, L.end(), 0.0f);
        std::vector<float> R (L.size(), 0.0f);
        for (int pos = 0; pos < (int) L.size(); pos += 256)
        {
            float* ch[2] { L.data() + pos, R.data() + pos };
            e.process (ch, 2, std::min (256, (int) L.size() - pos), 120.0, -1.0);
        }
        const auto side = [&] (double a, double b) { return rmsDb (L, (int) (a * fs), (int) (b * fs)) - rmsDb (R, (int) (a * fs), (int) (b * fs)); };
        check (side (0.3, 1.0) > 2.0, "Source a gauche : la reverb penche a gauche (gauche / droite)", side (0.3, 1.0), "dB");
        check (std::abs (side (1.2, 2.5)) < 1.5, "Source a gauche : la queue finit au centre (gauche / droite)", side (1.2, 2.5), "dB");
    }

    // Changer de mode et tirer SIZE pendant la queue : pas de clic (le plus grand saut d'un échantillon au suivant
    // reste du même ordre que sans aucun changement)
    {
        double jump[2] {};
        for (int moving = 0; moving < 2; ++moving)
        {
            auto p = wetOnly (plate1, 3.0f);
            e.prepare (fs, p);
            const auto out = run (e, noise ((int) (fs * 6.0), 0.2f, 41), nullptr, [&] (Engine& en, int block)
            {
                if (! moving) return;
                auto q = wetOnly ((block / 90) % numModes, 3.0f);    // un mode toutes les 0,48 s
                q.size = 0.5f + 0.5f * (float) std::sin (block * 0.05);  // SIZE qui bouge sans cesse
                en.set (q);
            });
            for (size_t i = (size_t) fs; i < out.size(); ++i) jump[moving] = std::max (jump[moving], (double) std::abs (out[i] - out[i - 1]));
        }
        check (jump[1] < 1.6 * jump[0], "Modes et SIZE qui changent : plus grand saut / sans changement", jump[1] / jump[0], "x");
    }

    // Charge : 20 s de stéréo, ONIRIQUE, modulation à fond
    {
        auto p = wetOnly (dream, 6.0f);
        p.modDepth = 1.0f;
        e.prepare (fs, p);
        const auto in = noise ((int) (fs * 20.0), 0.2f, 23);
        const auto t0 = juce::Time::getHighResolutionTicks();
        run (e, in);
        const double sec = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        std::printf ("Charge : %.0f x temps reel (%.1f ns par echantillon)\n\n", 20.0 / sec, sec * 1.0e9 / (20.0 * fs));
    }

    // Voix réelle : un fichier par mode, et la crête de sortie
    if (argc > 1)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        const juce::File src (juce::String::fromUTF8 (argv[1]));
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (src));
        if (reader == nullptr) { std::printf ("lecture impossible : %s\n", argv[1]); return 2; }
        juce::AudioBuffer<float> voice ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&voice, 0, voice.getNumSamples(), 0, true, true);
        const juce::File outDir = argc > 2 ? juce::File (juce::String::fromUTF8 (argv[2])) : src.getParentDirectory();
        outDir.createDirectory();

        for (int mode = 0; mode < numModes; ++mode)
        {
            Params p;
            p.mode = mode; p.mix = 0.35f;
            if (mode >= shimmer) p.decay = 5.0f;
            e.prepare (reader->sampleRate, p);
            juce::AudioBuffer<float> out (2, voice.getNumSamples() + (int) (reader->sampleRate * 3.0));
            out.clear();
            for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, voice, std::min (c, voice.getNumChannels() - 1), 0, voice.getNumSamples());
            for (int pos = 0; pos < out.getNumSamples(); pos += 512)
            {
                const int n = std::min (512, out.getNumSamples() - pos);
                float* ch[2] { out.getWritePointer (0) + pos, out.getWritePointer (1) + pos };
                e.process (ch, 2, n, 120.0, -1.0);
            }
            const auto file = outDir.getChildFile ("REVERB " + juce::String (modeNames[mode]) + ".wav");
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), reader->sampleRate, 2, 24, {}, 0));
            if (writer != nullptr) writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
            std::printf ("%-10s crete entree %.2f, sortie %.2f -> %s\n", modeNames[mode], voice.getMagnitude (0, voice.getNumSamples()),
                         out.getMagnitude (0, out.getNumSamples()), file.getFullPathName().toRawUTF8());
        }
    }

    std::printf (failures == 0 ? "\nTous les essais passent.\n" : "\n%d essai(s) en echec.\n", failures);
    return failures == 0 ? 0 : 1;
}
