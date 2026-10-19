"""Strumenti acustici ed elettrici (sintesi additiva, modale e Karplus-Strong)."""
import numpy as np
from dsp import *
from registry import sound

G = "Strumenti"

# ============================================================== Pianoforti e tastiere
KEYS = "Pianoforti e tastiere"


def piano_note(f, d, bright=1.0, seed=1, vel=0.8):
    t = tt(d)
    out = np.zeros_like(t)
    B = 0.00035 * (f / 261.6) ** 0.5           # inarmonicita' delle corde
    r = rng(seed)
    nmax = int(min(40, 9000 / f))
    for n in range(1, nmax + 1):
        fn = f * n * np.sqrt(1 + B * n * n)
        strike = abs(np.sin(np.pi * n / 7.3)) + 0.08          # punto di colpo del martelletto
        amp = strike / n ** (1.25 - 0.35 * bright * vel) * (1.0 if n < 3 else 0.9)
        tau_fast = 0.9 / (1 + 0.22 * (n - 1)) * (261.6 / f) ** 0.3
        tau_slow = 4.5 / (1 + 0.10 * (n - 1)) * (261.6 / f) ** 0.4
        env = 0.62 * np.exp(-t / tau_fast) + 0.38 * np.exp(-t / tau_slow)
        # tre corde per nota, leggermente scordate: il "battimento" del piano vero
        for c, det in enumerate((-0.9, 0.0, 1.1)):
            out += amp / 3 * env * np.sin(2 * np.pi * fn * cents(det + r.uniform(-0.2, 0.2)) * t + r.uniform(0, 6.28))
    # colpo del martelletto
    thump = lp(noise(d, seed + 9), 900) * np.exp(-t / 0.012) * 0.25
    knock = bp(noise(d, seed + 3), 2000, 6000) * np.exp(-t / 0.004) * 0.04 * bright
    x = out + thump + knock
    x = peak(x, 120, 1.0, 2.0)                  # tavola armonica
    return x


@sound("piano", "Pianoforte", G, KEYS, gated=True, release=0.35, colour=0xffd9d2c5)
def _():
    return finish(reverb(piano_note(mtof(60), 4.0), 0.18, 2.0, 0.45), max_len=4.5)


@sound("piano_bright", "Pianoforte brillante", G, KEYS, gated=True, release=0.3, colour=0xffe6e1d3)
def _():
    x = piano_note(mtof(60), 3.5, bright=1.6, seed=5, vel=1.0)
    x = peak(x, 3500, 0.8, 4.0)
    return finish(reverb(x, 0.14, 1.6, 0.35), max_len=4.0)


@sound("epiano", "Piano elettrico", G, KEYS, gated=True, release=0.3, colour=0xffd88f5a)
def _():
    f = mtof(60); d = 3.5; t = tt(d)
    idx = 2.2 * np.exp(-t / 0.35) + 0.25
    body = fm(f, d, 1.0, idx) * (0.75 * np.exp(-t / 1.6) + 0.25 * np.exp(-t / 0.4))
    tine = fm(f, d, 14.0, 1.2 * np.exp(-t / 0.02)) * np.exp(-t / 0.12) * 0.25
    bark = np.sin(2 * np.pi * f * 2 * t) * np.exp(-t / 0.3) * 0.12
    x = (body + tine + bark) * (1 + 0.06 * np.sin(2 * np.pi * 4.8 * t))
    x = drive(x * 0.9, 1.4)
    return finish(reverb(chorus(x, 3, 0.5, 0.35), 0.12, 1.2, 0.3), max_len=3.6)


@sound("organ", "Organo", G, KEYS, gated=True, release=0.06, colour=0xffc2763b)
def _():
    f = mtof(60); d = 2.5; t = tt(d)
    bars = [(0.5, 0.55), (1, 1.0), (1.5, 0.45), (2, 0.6), (3, 0.35), (4, 0.3), (6, 0.15), (8, 0.18)]
    x = sum(a * np.sin(2 * np.pi * f * r * t + i) for i, (r, a) in enumerate(bars))
    # percussione (attacco della 3a armonica) e click dei contatti
    x += 0.35 * np.sin(2 * np.pi * f * 3 * t) * np.exp(-t / 0.18)
    x += bp(noise(d, 4), 1500, 7000) * np.exp(-t / 0.003) * 0.6
    # Leslie: tremolo + leggera modulazione
    x = x * (1 + 0.12 * np.sin(2 * np.pi * 6.2 * t)) * adsr(d, 0.004, 0.1, 1.0)
    x = chorus(x, 1.5, 6.2, 0.4)
    return finish(drive(x * 0.5, 1.3), max_len=2.5)


@sound("church_organ", "Organo da chiesa", G, KEYS, gated=True, release=0.5, colour=0xffa9824f)
def _():
    f = mtof(60); d = 3.5; t = tt(d)
    ranks = [(0.5, 0.7), (1, 1.0), (2, 0.6), (3, 0.25), (4, 0.35), (6, 0.15), (8, 0.18), (12, 0.08), (16, 0.08)]
    x = np.zeros_like(t)
    for i, (r, a) in enumerate(ranks):
        # canne a ancia/flauto: un po' di rumore d'aria e piccole stonature tra le file
        x += a * np.sin(2 * np.pi * f * r * cents((i % 3 - 1) * 1.5) * t + i)
    x += 0.04 * bp(noise(d, 8), 400, 3000)
    x *= adsr(d, 0.09, 0.2, 1.0)
    return finish(reverb(x, 0.35, 3.5, 1.2, 4000), max_len=5.0)


@sound("harpsichord", "Clavicembalo", G, KEYS, gated=False, colour=0xffcbb27c)
def _():
    f = mtof(60)
    a = karplus(f, 2.5, 0.9985, 0.95, 0.08, seed=4, tilt=0.95)
    b = karplus(f * 2, 2.5, 0.998, 0.95, 0.1, seed=6, tilt=0.95) * 0.35
    x = mix_at(a, b)
    x = peak(x, 2500, 0.7, 4.0)
    return finish(reverb(x, 0.12, 1.2, 0.3), max_len=2.6)


@sound("music_box", "Carillon", G, KEYS, root=72, gated=False, colour=0xfff0d9a8)
def _():
    f = mtof(72); d = 2.5; t = tt(d)
    x = modal(f, d, [1, 2.0, 4.1, 6.8], [1, 0.35, 0.15, 0.06], [1.8, 0.6, 0.2, 0.08], seed=2)
    x += bp(noise(d, 2), 3000, 9000) * np.exp(-t / 0.002) * 0.3
    return finish(reverb(x, 0.25, 2.0, 0.6), max_len=3.0)


@sound("clavinet", "Clavinet", G, KEYS, gated=True, release=0.05, colour=0xffb98a5b)
def _():
    f = mtof(60)
    x = karplus(f, 1.6, 0.997, 0.9, 0.05, seed=12, tilt=0.9)
    x = peak(hp(x, 180), 1800, 1.2, 8.0)
    return finish(drive(x * 2, 1.6), max_len=1.6)


# ============================================================== Chitarre
GTR = "Chitarre"


def body_resonance(x):
    x = peak(x, 105, 2.5, 6.0)
    x = peak(x, 220, 2.0, 4.0)
    x = peak(x, 450, 1.5, 2.0)
    return peak(x, 3200, 1.0, 2.5)


@sound("guitar_acoustic", "Chitarra acustica", G, GTR, root=48, gated=True, release=0.25, colour=0xffc8955c)
def _():
    f = mtof(48)
    x = karplus(f, 3.0, 0.9975, 0.55, 0.16, seed=21, tilt=0.8)
    x = body_resonance(x)
    t = tt(0.05)
    x[:len(t)] += bp(noise(0.05, 2), 2000, 8000) * np.exp(-t / 0.006) * 0.15
    return finish(reverb(x, 0.12, 1.4, 0.35), max_len=3.2)


@sound("guitar_nylon", "Chitarra classica", G, GTR, root=48, gated=True, release=0.25, colour=0xffd4a46b)
def _():
    f = mtof(48)
    x = karplus(f, 3.0, 0.997, 0.3, 0.2, seed=22, tilt=0.6)
    x = lp(body_resonance(x), 4500)
    return finish(reverb(x, 0.15, 1.5, 0.4), max_len=3.2)


@sound("guitar_clean", "Chitarra elettrica pulita", G, GTR, root=48, gated=True, release=0.15, colour=0xff7f9fd0)
def _():
    f = mtof(48)
    x = karplus(f, 3.0, 0.9985, 0.75, 0.12, seed=23, tilt=0.85)
    pick = 0.25
    m = secs(1 / f * pick)
    x = x - 0.6 * np.concatenate([np.zeros(m), x[:-m]])   # posizione del pickup
    x = lp(peak(x, 2200, 0.8, 3.0), 5500)
    x = chorus(x, 4, 0.8, 0.35)
    return finish(reverb(x, 0.15, 1.4, 0.35), max_len=3.0)


@sound("guitar_dist", "Chitarra distorta (power chord)", G, GTR, root=40, gated=True, release=0.08, colour=0xffcf5a45)
def _():
    f = mtof(40)
    x = mix_at(karplus(f, 2.5, 0.999, 0.8, 0.13, seed=31),
               karplus(f * 1.4983, 2.5, 0.999, 0.8, 0.13, seed=32) * 0.85,
               karplus(f * 2, 2.5, 0.999, 0.8, 0.13, seed=33) * 0.6)
    x = hp(x, 90)
    x = drive(x * 9, 3.0)
    x = drive(peak(x, 800, 0.8, 4.0) * 1.5, 1.5)
    x = lp(peak(x, 2500, 1.0, 3.0), 4800, order=4)   # cassa 4x12
    return finish(reverb(x, 0.08, 0.8, 0.2), max_len=2.6)


@sound("guitar_mute", "Chitarra stoppata", G, GTR, root=40, gated=False, colour=0xffb4523f)
def _():
    f = mtof(40)
    x = mix_at(karplus(f, 0.5, 0.985, 0.5, 0.13, seed=41), karplus(f * 1.4983, 0.5, 0.985, 0.5, 0.13, seed=42) * 0.8)
    x *= expenv(len(x) / SR, 0.09)
    x = drive(hp(x, 90) * 8, 3.0)
    x = lp(peak(x, 1500, 1.0, 3.0), 4000, order=4)
    return finish(x, max_len=0.5)


@sound("harp", "Arpa", G, GTR, gated=False, colour=0xffe2c38c)
def _():
    f = mtof(60)
    x = karplus(f, 3.5, 0.9988, 0.25, 0.3, seed=51, tilt=0.55)
    x = peak(x, 300, 1.0, 2.0)
    return finish(reverb(x, 0.3, 2.5, 0.8), max_len=4.0)


@sound("sitar", "Sitar", G, GTR, gated=False, colour=0xffc9883e)
def _():
    f = mtof(60); d = 3.0
    x = karplus(f, d, 0.9985, 0.9, 0.1, seed=61, tilt=0.9)
    # ponte "jawari": distorsione asimmetrica che fa ronzare la corda
    x = np.where(x > 0, np.tanh(x * 3) / 3 * 1.5, x)
    x += 0.4 * karplus(f * 2.0, d, 0.9992, 0.4, 0.3, seed=62) * expenv(d, 1.5)  # corde simpatiche
    return finish(reverb(peak(x, 2500, 1.0, 5.0), 0.2, 2.0, 0.6), max_len=3.2)


# ============================================================== Bassi
BASS = "Bassi"


@sound("bass_finger", "Basso elettrico", G, BASS, root=36, gated=True, release=0.08, colour=0xff8a6bd1)
def _():
    f = mtof(36)
    x = karplus(f, 2.5, 0.9975, 0.35, 0.2, seed=71, tilt=0.35)
    x = lp(peak(x, 700, 1.0, 3.0), 2500)
    return finish(drive(x * 1.5, 1.2), hpf=30, max_len=2.5)


@sound("bass_pick", "Basso col plettro", G, BASS, root=36, gated=True, release=0.08, colour=0xff7d5fc4)
def _():
    f = mtof(36)
    x = karplus(f, 2.0, 0.998, 0.8, 0.1, seed=72, tilt=0.6)
    x = peak(lp(x, 4000), 1200, 1.0, 5.0)
    return finish(drive(x * 2.0, 1.4), hpf=30, max_len=2.0)


@sound("bass_slap", "Basso slap", G, BASS, root=36, gated=True, release=0.06, colour=0xff9a72e0)
def _():
    f = mtof(36); d = 1.6; t = tt(d)
    x = karplus(f, d, 0.9965, 0.95, 0.06, seed=73, tilt=0.7)
    x += bp(noise(d, 74), 1500, 6000) * np.exp(-t / 0.006) * 0.6
    x = peak(peak(x, 80, 1.0, 4.0), 2500, 0.8, 6.0)    # "scooped": bassi e acuti su
    return finish(drive(x * 1.5, 1.5), hpf=30, max_len=1.6)


@sound("bass_upright", "Contrabbasso", G, BASS, root=36, gated=True, release=0.1, colour=0xff6e57a8)
def _():
    f = mtof(36); d = 2.0; t = tt(d)
    x = karplus(f, d, 0.995, 0.2, 0.3, seed=75, tilt=0.25)
    x += lp(noise(d, 76), 300) * np.exp(-t / 0.02) * 0.6
    x = lp(peak(x, 160, 1.5, 4.0), 1800)
    return finish(reverb(x, 0.08, 0.8, 0.25), hpf=30, max_len=2.0)


@sound("bass_fretless", "Basso fretless", G, BASS, root=36, gated=True, release=0.12, colour=0xff8064c0)
def _():
    f = mtof(36)
    x = karplus(f, 2.8, 0.9982, 0.4, 0.25, seed=77, tilt=0.4)
    x = peak(lp(x, 2200), 500, 1.5, 6.0)   # "mwah"
    x = chorus(x, 2, 0.5, 0.3)
    return finish(x, hpf=30, max_len=2.8)


# ============================================================== Archi
STR = "Archi"


def string_section(f, d, voices=8, attack=0.25, cutoff=3200, seed=0):
    t = tt(d)
    x = np.zeros_like(t)
    r = rng(seed)
    for v in range(voices):
        det = r.uniform(-11, 11)
        fr = vibrato(d, f * cents(det), r.uniform(4.8, 6.0), r.uniform(6, 14), 0.15, seed + v)
        x += saw(fr, d, seed=seed + v)
    x /= voices ** 0.5
    x = lp(x, cutoff, order=2)
    x = peak(x, 300, 1.0, 3.0)
    x = peak(x, 2800, 1.5, 3.0)
    x += 0.03 * bp(noise(d, seed + 50), 2000, 7000) * adsr(d, attack, 0.3, 0.5)   # crine sull'arco
    return x * adsr(d, attack, 0.6, 0.85)


@sound("strings", "Archi (ensemble)", G, STR, gated=True, release=0.45, colour=0xffc77b5c)
def _():
    return finish(reverb(string_section(mtof(60), 3.5, seed=81), 0.3, 2.5, 0.8), max_len=4.5)


@sound("strings_fast", "Archi staccato", G, STR, gated=True, release=0.15, colour=0xffb86e52)
def _():
    d = 0.8
    x = string_section(mtof(60), d, attack=0.02, cutoff=4500, seed=82) * expenv(d, 0.2)
    return finish(reverb(x, 0.3, 1.8, 0.6), max_len=1.6)


@sound("pizzicato", "Archi pizzicato", G, STR, gated=False, colour=0xffd18a68)
def _():
    f = mtof(60)
    x = mix_at(*[karplus(f * cents(c), 1.2, 0.993, 0.3, 0.25, seed=83 + i) for i, c in enumerate((-6, 0, 5, 9))])
    x = lp(body_resonance(x), 3500)
    return finish(reverb(x, 0.3, 1.6, 0.5), max_len=1.8)


@sound("violin", "Violino", G, STR, root=67, gated=True, release=0.25, colour=0xffb5603d)
def _():
    f = mtof(67); d = 3.0
    fr = vibrato(d, f, 5.6, 18, 0.3, 3)
    x = saw(fr, d, seed=91)
    x = formants(x, [290, 500, 1200, 3000], [80, 150, 300, 800], [1.0, 0.8, 0.5, 0.6]) + 0.25 * lp(x, 6000)
    x += 0.025 * bp(noise(d, 92), 2500, 8000)
    x *= adsr(d, 0.12, 0.5, 0.9)
    return finish(reverb(x, 0.22, 2.0, 0.6), max_len=3.5)


@sound("cello", "Violoncello", G, STR, root=48, gated=True, release=0.3, colour=0xff9b5236)
def _():
    f = mtof(48); d = 3.0
    fr = vibrato(d, f, 5.0, 14, 0.35, 4)
    x = saw(fr, d, seed=93)
    x = formants(x, [200, 400, 900, 2500], [70, 120, 250, 700], [1.0, 0.9, 0.5, 0.35]) + 0.2 * lp(x, 4000)
    x *= adsr(d, 0.15, 0.5, 0.9)
    return finish(reverb(x, 0.22, 2.0, 0.6), max_len=3.5)


# ============================================================== Fiati e ottoni
WIND = "Fiati e ottoni"


def brass_voice(f, d, bright=3500, seed=0):
    t = tt(d)
    # attacco: un piccolo "scoop" di intonazione e il filtro che si apre
    pitch = f * cents(-35 * np.exp(-t / 0.03))
    fr = vibrato(d, pitch, 5.2, 8, 0.4, seed)
    x = saw(fr, d, seed=seed)
    cutoff = 500 + bright * (1 - np.exp(-t / 0.05)) * (0.75 + 0.25 * np.exp(-t / 0.3))
    return svf(x, cutoff, 0.9)


@sound("brass", "Ottoni (sezione)", G, WIND, gated=True, release=0.12, colour=0xffe0a53c)
def _():
    d = 2.5
    x = sum(brass_voice(mtof(60) * cents(c), d, 3200, seed=101 + i) for i, c in enumerate((-8, -2, 4, 9)))
    x = drive(x * 0.6, 1.4) * adsr(d, 0.03, 0.4, 0.85)
    return finish(reverb(x, 0.22, 1.8, 0.5), max_len=3.0)


@sound("brass_stab", "Ottoni stab", G, WIND, gated=False, colour=0xffeab248)
def _():
    d = 0.7
    x = sum(brass_voice(mtof(60) * cents(c), d, 5000, seed=111 + i) for i, c in enumerate((-8, -2, 4, 9)))
    x = drive(x * 0.8, 1.6) * expenv(d, 0.18)
    return finish(reverb(x, 0.25, 1.4, 0.4), max_len=1.5)


@sound("trumpet", "Tromba", G, WIND, root=67, gated=True, release=0.1, colour=0xfff0bf4a)
def _():
    d = 2.5
    x = brass_voice(mtof(67), d, 5000, seed=121)
    x = peak(x, 1300, 1.0, 5.0) * adsr(d, 0.025, 0.4, 0.85)
    return finish(reverb(drive(x, 1.3), 0.18, 1.6, 0.45), max_len=3.0)


@sound("trombone", "Trombone", G, WIND, root=48, gated=True, release=0.12, colour=0xffd59a36)
def _():
    d = 2.5
    x = brass_voice(mtof(48), d, 2500, seed=122)
    x = peak(x, 600, 1.0, 4.0) * adsr(d, 0.04, 0.4, 0.85)
    return finish(reverb(drive(x, 1.3), 0.18, 1.6, 0.45), max_len=3.0)


@sound("flute", "Flauto", G, WIND, root=72, gated=True, release=0.15, colour=0xffa7d4e8)
def _():
    f = mtof(72); d = 2.5; t = tt(d)
    fr = vibrato(d, f, 5.0, 10, 0.3, 5)
    x = sine(fr, d) + 0.18 * sine(fr * 2, d, 1.0) + 0.05 * sine(fr * 3, d, 2.0)
    breath = bp(noise(d, 131), f * 0.8, f * 4) * 0.12
    x = (x + breath) * adsr(d, 0.08, 0.3, 0.9)
    x[: secs(0.06)] += bp(noise(0.06, 132), 1000, 6000) * np.linspace(1, 0, secs(0.06)) * 0.15   # "chiff"
    return finish(reverb(x, 0.25, 1.8, 0.5), max_len=3.0)


@sound("pan_flute", "Flauto di Pan", G, WIND, root=72, gated=True, release=0.2, colour=0xff92c4d6)
def _():
    f = mtof(72); d = 2.0; t = tt(d)
    x = sine(vibrato(d, f, 4.5, 6, 0.4, 6), d) + 0.06 * sine(f * 3, d)
    x += bp(noise(d, 133), f * 0.7, f * 3) * (0.25 * np.exp(-t / 0.08) + 0.08)
    x *= adsr(d, 0.05, 0.2, 0.85)
    return finish(reverb(x, 0.3, 2.0, 0.6), max_len=2.5)


@sound("clarinet", "Clarinetto", G, WIND, gated=True, release=0.12, colour=0xff7aa6bd)
def _():
    f = mtof(60); d = 2.5
    fr = vibrato(d, f, 4.8, 5, 0.5, 7)
    x = additive(fr, d, lambda k: (1 / k if k % 2 else 0.04 / k))
    x = lp(x, 2800) * adsr(d, 0.05, 0.3, 0.9)
    x += 0.02 * bp(noise(d, 141), 1500, 5000)
    return finish(reverb(x, 0.2, 1.6, 0.45), max_len=3.0)


@sound("sax", "Sax", G, WIND, gated=True, release=0.12, colour=0xffd6a05c)
def _():
    f = mtof(60); d = 2.5; t = tt(d)
    fr = vibrato(d, f * cents(-20 * np.exp(-t / 0.05)), 5.2, 14, 0.35, 8)
    x = saw(fr, d, seed=151)
    x = formants(x, [500, 1450, 2600], [200, 300, 500], [1.0, 0.7, 0.4]) + 0.3 * lp(x, 3000)
    x += 0.05 * bp(noise(d, 152), 1500, 6000) * adsr(d, 0.03, 0.2, 0.4)
    x = drive(x * 1.5, 1.6) * adsr(d, 0.04, 0.3, 0.85)
    return finish(reverb(x, 0.2, 1.6, 0.45), max_len=3.0)


@sound("harmonica", "Armonica", G, WIND, gated=True, release=0.08, colour=0xffb7b7b7)
def _():
    f = mtof(60); d = 2.0
    x = pulse(vibrato(d, f, 5.5, 6, 0.3, 9), d, 0.3, seed=161) + 0.4 * pulse(f * 2 * cents(4), d, 0.2, seed=162)
    x = peak(lp(x, 4000), 1800, 1.0, 5.0) * adsr(d, 0.04, 0.3, 0.8)
    return finish(reverb(x, 0.12, 1.0, 0.3), max_len=2.2)


# ============================================================== Voci
VOX = "Voci e cori"


def choir(f, d, vowel, voices=6, seed=0):
    freqs, bws, gains = VOWELS[vowel]
    r = rng(seed)
    x = np.zeros(secs(d))
    for v in range(voices):
        fr = vibrato(d, f * cents(r.uniform(-12, 12)), r.uniform(4.5, 6.0), r.uniform(8, 18), 0.2, seed + v)
        src = additive(fr, d, lambda k: 1.0 / k ** 1.1, phase0=1, seed=seed + v)
        x += formants(src, freqs, bws, gains)
    x /= voices ** 0.5
    x += 0.02 * hp(noise(d, seed + 99), 3000)
    return x * adsr(d, 0.3, 0.5, 0.9)


@sound("choir_aah", "Coro \"Aah\"", G, VOX, gated=True, release=0.5, colour=0xffe3b8d8)
def _():
    return finish(reverb(choir(mtof(60), 3.5, "a", seed=171), 0.4, 3.0, 1.0), max_len=4.5)


@sound("choir_ooh", "Coro \"Ooh\"", G, VOX, gated=True, release=0.5, colour=0xffd4a5c9)
def _():
    return finish(reverb(choir(mtof(60), 3.5, "u", seed=181), 0.4, 3.0, 1.0), max_len=4.5)


@sound("voice_solo", "Voce solista \"Ah\"", G, VOX, root=67, gated=True, release=0.25, colour=0xffeac5e0)
def _():
    return finish(reverb(choir(mtof(67), 3.0, "a", voices=1, seed=191), 0.3, 2.0, 0.7), max_len=3.5)


# ============================================================== Percussioni melodiche
MALLET = "Percussioni melodiche"


def mallet_click(d, lo=1500, hi=8000, tau=0.002, seed=0):
    t = tt(d)
    return bp(noise(d, seed), lo, hi) * np.exp(-t / tau)


@sound("marimba", "Marimba", G, MALLET, gated=False, colour=0xffb87a46)
def _():
    f = mtof(60); d = 2.0
    x = modal(f, d, [1, 3.93, 9.2], [1, 0.35, 0.1], [0.75, 0.15, 0.04], seed=201)
    x += mallet_click(d, 300, 2500, 0.004, 202) * 0.3
    return finish(reverb(x, 0.18, 1.4, 0.4), max_len=2.0)


@sound("vibraphone", "Vibrafono", G, MALLET, gated=True, release=0.6, colour=0xff8fb8c9)
def _():
    f = mtof(60); d = 4.0; t = tt(d)
    x = modal(f, d, [1, 4.0, 10.0], [1, 0.25, 0.06], [3.2, 0.8, 0.2], seed=203)
    x *= 1 + 0.35 * np.sin(2 * np.pi * 5.5 * t)            # motorino del vibrafono
    x += mallet_click(d, 1000, 5000, 0.002, 204) * 0.2
    return finish(reverb(x, 0.22, 2.0, 0.6), max_len=4.5)


@sound("xylophone", "Xilofono", G, MALLET, root=72, gated=False, colour=0xffd2a462)
def _():
    f = mtof(72); d = 1.2
    x = modal(f, d, [1, 3.0, 6.0], [1, 0.35, 0.1], [0.35, 0.1, 0.04], seed=205)
    x += mallet_click(d, 2000, 9000, 0.0015, 206) * 0.4
    return finish(reverb(x, 0.15, 1.2, 0.35), max_len=1.4)


@sound("glockenspiel", "Glockenspiel", G, MALLET, root=84, gated=False, colour=0xffdde6ee)
def _():
    f = mtof(84); d = 2.8
    x = modal(f, d, [1, 2.76, 5.40, 8.93], [1, 0.4, 0.15, 0.06], [1.8, 0.7, 0.25, 0.12], seed=207)
    x += mallet_click(d, 4000, 12000, 0.001, 208) * 0.2
    return finish(reverb(x, 0.2, 1.8, 0.5), max_len=3.0)


@sound("celesta", "Celesta", G, MALLET, root=72, gated=False, colour=0xffe8dcc4)
def _():
    f = mtof(72); d = 2.2
    x = modal(f, d, [1, 2.0, 3.0, 4.15], [1, 0.25, 0.08, 0.05], [1.2, 0.4, 0.15, 0.08], seed=209)
    x += mallet_click(d, 1000, 4000, 0.002, 210) * 0.15
    return finish(reverb(x, 0.25, 1.8, 0.5), max_len=2.5)


@sound("tubular_bells", "Campane tubolari", G, MALLET, gated=False, colour=0xffc9c2b0)
def _():
    f = mtof(60); d = 5.0
    ratios = [1.0, 2.0, 3.01, 4.17, 5.43, 6.80, 8.21]
    x = modal(f / 2, d, [r * 2 for r in ratios], [0.5, 1.0, 0.7, 0.5, 0.35, 0.2, 0.12],
              [4.5, 3.5, 2.8, 2.0, 1.4, 1.0, 0.7], seed=211, detune_cents=2)
    x += mallet_click(d, 2000, 8000, 0.002, 212) * 0.2
    return finish(reverb(x, 0.25, 3.0, 1.0), max_len=5.5)


@sound("kalimba", "Kalimba", G, MALLET, root=72, gated=False, colour=0xffc49360)
def _():
    f = mtof(72); d = 1.8
    x = modal(f, d, [1, 5.9, 8.8], [1, 0.25, 0.06], [0.9, 0.08, 0.04], seed=213)
    x += mallet_click(d, 400, 3000, 0.003, 214) * 0.2
    x = peak(x, 250, 1.5, 3.0)
    return finish(reverb(x, 0.2, 1.4, 0.4), max_len=2.0)


@sound("steel_drum", "Steel drum", G, MALLET, gated=False, colour=0xffd7c25e)
def _():
    f = mtof(60); d = 2.2; t = tt(d)
    fr = f * cents(25 * np.exp(-t / 0.01))
    x = (sine(fr, d) * np.exp(-t / 1.1) + 0.55 * sine(fr * 2, d, 1) * np.exp(-t / 0.7)
         + 0.25 * sine(fr * 3, d, 2) * np.exp(-t / 0.4) + 0.12 * sine(fr * 4.02, d, 3) * np.exp(-t / 0.25))
    x += mallet_click(d, 500, 3000, 0.003, 215) * 0.2
    return finish(reverb(x, 0.2, 1.6, 0.45), max_len=2.4)


@sound("timpani", "Timpano", G, MALLET, root=43, gated=False, colour=0xff9d7d5a)
def _():
    f = mtof(43); d = 3.0; t = tt(d)
    fr = f * cents(30 * np.exp(-t / 0.05))
    x = (sine(fr, d) * np.exp(-t / 1.6) + 0.5 * sine(fr * 1.504, d, 1) * np.exp(-t / 1.1)
         + 0.35 * sine(fr * 1.742, d, 2) * np.exp(-t / 0.9) + 0.25 * sine(fr * 2.0, d, 3) * np.exp(-t / 0.7)
         + 0.15 * sine(fr * 2.245, d, 4) * np.exp(-t / 0.5))
    x += lp(noise(d, 216), 600) * np.exp(-t / 0.03) * 0.5
    return finish(reverb(x, 0.25, 2.5, 0.8), hpf=30, max_len=3.5)


@sound("handpan", "Handpan", G, MALLET, gated=False, colour=0xffa98f6c)
def _():
    f = mtof(60); d = 3.0
    x = modal(f, d, [1, 2.0, 3.0, 4.95], [1, 0.45, 0.25, 0.08], [2.0, 1.2, 0.6, 0.2], seed=217, detune_cents=3)
    x += mallet_click(d, 200, 1500, 0.006, 218) * 0.3
    return finish(reverb(x, 0.3, 2.5, 0.8), max_len=3.5)


# ============================================================== Batteria acustica
DRUMS = "Batteria acustica"


def room(x, amount=0.12):
    return reverb(x, amount, 0.9, 0.18, 7000, seed=301)


@sound("ac_kick", "Cassa", G, DRUMS, colour=0xffe8833a)
def _():
    d = 0.9; t = tt(d)
    f = 58 + 45 * np.exp(-t / 0.018)
    x = sine(f, d) * np.exp(-t / 0.22)
    x += lp(noise(d, 302), 180) * np.exp(-t / 0.05) * 1.2          # pelle e fusto
    x += bp(noise(d, 303), 2500, 6000) * np.exp(-t / 0.0035) * 0.6  # battente
    return finish(room(drive(x, 1.4), 0.08), max_len=0.9)


@sound("ac_kick_tight", "Cassa secca", G, DRUMS, colour=0xffd97a35)
def _():
    d = 0.5; t = tt(d)
    f = 65 + 60 * np.exp(-t / 0.012)
    x = sine(f, d) * np.exp(-t / 0.12)
    x += bp(noise(d, 304), 3000, 8000) * np.exp(-t / 0.003) * 0.9
    return finish(drive(x, 1.8), max_len=0.5)


def snare_ac(d, tone=185, wires=1.0, tau=0.16, seed=0, crack=0.5):
    t = tt(d)
    x = sine(tone * (1 + 0.15 * np.exp(-t / 0.01)), d) * np.exp(-t / 0.07)
    x += 0.5 * sine(tone * 1.78, d, 1) * np.exp(-t / 0.04)
    sn = bp(noise(d, seed), 1800, 9000) * np.exp(-t / tau) * wires
    x += sn
    x += hp(noise(d, seed + 1), 4000) * np.exp(-t / 0.004) * crack
    return x


@sound("ac_snare", "Rullante", G, DRUMS, colour=0xffd2555a)
def _():
    return finish(room(drive(snare_ac(1.0, seed=311), 1.5)), max_len=1.0)


@sound("ac_snare_rim", "Rullante cerchio (rimshot)", G, DRUMS, colour=0xffc94a4f)
def _():
    d = 1.0; t = tt(d)
    x = snare_ac(d, 210, 0.8, 0.13, 312, crack=1.2)
    x += bp(noise(d, 313), 900, 2200) * np.exp(-t / 0.008) * 1.2
    x += sine(470, d) * np.exp(-t / 0.02) * 0.6
    return finish(room(drive(x, 2.0)), max_len=1.0)


@sound("ac_sidestick", "Side stick", G, DRUMS, colour=0xffb85a4f)
def _():
    d = 0.3; t = tt(d)
    x = bp(noise(d, 314), 1500, 2600) * np.exp(-t / 0.012) + sine(1000, d) * np.exp(-t / 0.01) * 0.5
    x += sine(380, d) * np.exp(-t / 0.03) * 0.3
    return finish(room(x, 0.15), max_len=0.4)


@sound("ac_brush", "Spazzola", G, DRUMS, colour=0xffc77070)
def _():
    d = 0.6; t = tt(d)
    env = np.clip(t / 0.03, 0, 1) * np.exp(-t / 0.12)
    x = bp(noise(d, 315), 2000, 9000) * env
    return finish(room(x, 0.1), max_len=0.6)


def cymbal(d, lo, hi, n, tau, seed, noise_amt=0.3, hpf=3000):
    t = tt(d)
    x = metallic(d, lo, hi, n, tau, seed)
    x += hp(noise(d, seed + 1), hpf) * np.exp(-t / (tau * 0.6)) * noise_amt
    return hp(x, hpf * 0.8)


@sound("ac_hat_closed", "Hi-hat chiuso", G, DRUMS, colour=0xff4fb3a9)
def _():
    d = 0.3; t = tt(d)
    x = cymbal(d, 3500, 15000, 50, 0.04, 321, 0.5, 6000)
    x += hp(noise(d, 322), 8000) * np.exp(-t / 0.004) * 0.6
    return finish(x, max_len=0.3)


@sound("ac_hat_open", "Hi-hat aperto", G, DRUMS, colour=0xff5b8fd6)
def _():
    d = 1.3
    return finish(cymbal(d, 3000, 15000, 60, 0.45, 323, 0.45, 5000), max_len=1.3)


@sound("ac_hat_pedal", "Hi-hat a pedale", G, DRUMS, colour=0xff4aa39a)
def _():
    d = 0.25; t = tt(d)
    x = cymbal(d, 3000, 12000, 40, 0.03, 324, 0.3, 5000) * 0.6
    x += bp(noise(d, 325), 400, 1500) * np.exp(-t / 0.01) * 0.6        # i piatti che si chiudono
    return finish(x, max_len=0.25)


@sound("ac_ride", "Ride", G, DRUMS, colour=0xff7fa5c9)
def _():
    d = 3.5; t = tt(d)
    x = cymbal(d, 1500, 13000, 90, 1.6, 331, 0.15, 2500)
    x += modal(1, d, [3200, 4700, 6100], [0.35, 0.25, 0.15], [0.6, 0.4, 0.3], seed=332) * 0.6   # "ping"
    x += hp(noise(d, 333), 5000) * np.exp(-t / 0.004) * 0.5
    return finish(x, max_len=3.5)


@sound("ac_ride_bell", "Ride campana", G, DRUMS, colour=0xff8bb3d6)
def _():
    d = 3.0
    x = modal(1, d, [780, 1170, 1800, 2610, 3500], [1, 0.7, 0.5, 0.35, 0.2], [1.8, 1.4, 1.0, 0.7, 0.5], seed=334)
    x += cymbal(d, 2000, 12000, 50, 1.2, 335, 0.1, 2500) * 0.4
    return finish(x, max_len=3.0)


@sound("ac_crash", "Crash", G, DRUMS, colour=0xff6b9bc3)
def _():
    d = 4.0; t = tt(d)
    x = cymbal(d, 800, 16000, 120, 1.3, 341, 0.8, 1500)
    x *= 1 - 0.6 * np.exp(-t / 0.01)
    return finish(x, max_len=4.0)


@sound("ac_china", "China", G, DRUMS, colour=0xff5f8ab0)
def _():
    d = 2.5
    x = cymbal(d, 600, 9000, 90, 0.8, 342, 1.0, 1200)
    x = drive(x * 3, 2.0)
    return finish(x, max_len=2.5)


@sound("ac_splash", "Splash", G, DRUMS, colour=0xff77a6cf)
def _():
    d = 1.4
    return finish(cymbal(d, 2500, 16000, 70, 0.35, 343, 0.8, 3000), max_len=1.4)


def tom(f, d, seed):
    t = tt(d)
    fr = f * (1 + 0.35 * np.exp(-t / 0.04))
    x = sine(fr, d) * np.exp(-t / 0.35) + 0.4 * sine(fr * 1.59, d, 1) * np.exp(-t / 0.15)
    x += bp(noise(d, seed), 2000, 6000) * np.exp(-t / 0.004) * 0.5
    x += lp(noise(d, seed + 1), 500) * np.exp(-t / 0.03) * 0.4
    return finish(room(drive(x, 1.3)), max_len=d)


@sound("ac_tom_high", "Tom alto", G, DRUMS, colour=0xff9a78d0)
def _():
    return tom(210, 1.2, 351)


@sound("ac_tom_mid", "Tom medio", G, DRUMS, colour=0xff8d6cc2)
def _():
    return tom(150, 1.3, 352)


@sound("ac_tom_floor", "Timpano (floor tom)", G, DRUMS, colour=0xff7f60b3)
def _():
    return tom(98, 1.5, 353)


# ============================================================== Percussioni
PERC = "Percussioni"


@sound("shaker", "Shaker", G, PERC, colour=0xffbfae6f)
def _():
    d = 0.25; t = tt(d)
    env = (t / 0.02) * np.exp(1 - t / 0.02)
    return finish(bp(noise(d, 401), 4500, 12000) * env, max_len=0.25)


@sound("tambourine", "Tamburello", G, PERC, colour=0xffcdb25a)
def _():
    d = 0.7; t = tt(d)
    jingles = metallic(d, 5000, 12000, 40, 0.18, 402) * (0.6 + 0.4 * np.sin(2 * np.pi * 28 * t) ** 2)
    skin = lp(noise(d, 403), 1200) * np.exp(-t / 0.015) * 0.5
    return finish(hp(jingles, 3000) + skin, max_len=0.7)


@sound("cowbell", "Campanaccio", G, PERC, colour=0xffd6a24a)
def _():
    d = 0.6; t = tt(d)
    x = (square(562, d, rand_phase=False) + square(845, d, rand_phase=False)) * 0.5
    x = bp(x, 450, 3500) * (0.8 * np.exp(-t / 0.08) + 0.2 * np.exp(-t / 0.3))
    x += bp(noise(d, 404), 2000, 6000) * np.exp(-t / 0.002) * 0.3
    return finish(x, max_len=0.6)


def conga(f, d, slap, seed):
    t = tt(d)
    fr = f * (1 + 0.08 * np.exp(-t / 0.02))
    x = sine(fr, d) * np.exp(-t / 0.22) + 0.3 * sine(fr * 1.6, d) * np.exp(-t / 0.08)
    x += bp(noise(d, seed), 800, 5000) * np.exp(-t / (0.006 + 0.01 * slap)) * (0.3 + slap)
    return finish(room(x, 0.1), max_len=d)


@sound("conga_high", "Conga alta", G, PERC, colour=0xffb98a55)
def _():
    return conga(330, 0.7, 0.2, 405)


@sound("conga_low", "Conga bassa", G, PERC, colour=0xffa87d4c)
def _():
    return conga(220, 0.8, 0.1, 406)


@sound("conga_slap", "Conga slap", G, PERC, colour=0xffc49762)
def _():
    return conga(350, 0.5, 0.9, 407)


@sound("bongo", "Bongo", G, PERC, colour=0xffc9a06a)
def _():
    return conga(480, 0.45, 0.35, 408)


@sound("djembe", "Djembe", G, PERC, colour=0xff9e7448)
def _():
    d = 0.9; t = tt(d)
    fr = 85 * (1 + 0.25 * np.exp(-t / 0.03))
    x = sine(fr, d) * np.exp(-t / 0.3) + bp(noise(d, 409), 300, 2500) * np.exp(-t / 0.02) * 0.6
    return finish(room(drive(x, 1.3), 0.1), hpf=30, max_len=0.9)


@sound("claves", "Claves", G, PERC, colour=0xffd8b989)
def _():
    d = 0.25; t = tt(d)
    x = sine(2500, d) * np.exp(-t / 0.035) + 0.3 * sine(5250, d) * np.exp(-t / 0.015)
    return finish(room(x, 0.12), max_len=0.3)


@sound("woodblock", "Woodblock", G, PERC, colour=0xffcf9f6d)
def _():
    d = 0.25; t = tt(d)
    x = sine(1150, d) * np.exp(-t / 0.03) + bp(noise(d, 410), 800, 3000) * np.exp(-t / 0.006) * 0.4
    return finish(room(x, 0.1), max_len=0.3)


@sound("triangle", "Triangolo", G, PERC, colour=0xffe9e3cf)
def _():
    d = 3.0
    x = modal(1, d, [4700, 6900, 9200, 11800, 14200], [1, 0.7, 0.5, 0.3, 0.2], [2.5, 1.8, 1.3, 0.9, 0.6], seed=411)
    return finish(x, max_len=3.0)


@sound("guiro", "Guiro", G, PERC, colour=0xffb59a6d)
def _():
    d = 0.35
    x = np.zeros(secs(d))
    for i in range(14):
        s = secs(0.02 * i)
        x[s:s + secs(0.012)] += bp(noise(0.012, 412 + i), 2000, 7000) * np.hanning(secs(0.012)) * (0.6 + 0.4 * (i / 14))
    return finish(x, max_len=0.35)


@sound("cabasa", "Cabasa", G, PERC, colour=0xffc2b07a)
def _():
    d = 0.3; t = tt(d)
    env = np.clip(t / 0.04, 0, 1) * np.exp(-t / 0.06)
    return finish(hp(noise(d, 430), 6000) * env, max_len=0.3)


@sound("finger_snap", "Schiocco di dita", G, PERC, colour=0xffd5c2a8)
def _():
    d = 0.3; t = tt(d)
    x = bp(noise(d, 431), 1800, 4500) * np.exp(-t / 0.012) + sine(2100, d) * np.exp(-t / 0.008) * 0.3
    return finish(room(x, 0.18), max_len=0.4)


@sound("hand_clap", "Battito di mani", G, PERC, colour=0xffd8b14a)
def _():
    d = 0.6; t = tt(d)
    x = np.zeros(secs(d))
    for i, off in enumerate((0, 0.008, 0.015, 0.024)):
        s = secs(off)
        n = len(x) - s
        x[s:] += bp(noise(n / SR, 440 + i), 900, 3500) * np.exp(-np.arange(n) / SR / (0.006 if i < 3 else 0.06))
    return finish(room(x, 0.25), max_len=0.6)
