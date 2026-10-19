"""Suoni per la musica elettronica, divisi per genere, piu' synth/pad ed effetti."""
import numpy as np
from dsp import *
from registry import sound

G = "Elettronica"


# ------------------------------------------------------------------ mattoncini

def kick(d, f0, f1, ptau, atau, click=0.5, drv=2.0, sustain=0.0, seed=0, click_hi=6000):
    t = tt(d)
    f = f1 + (f0 - f1) * np.exp(-t / ptau)
    env = np.exp(-t / atau) * (1 - sustain) + sustain * np.exp(-t / (atau * 3))
    x = sine(f, d) * env
    x += bp(noise(d, seed), 1500, click_hi) * np.exp(-t / 0.003) * click
    return drive(x, drv)


def clap(d, bursts=4, spacing=0.009, f_lo=900, f_hi=2800, tail=0.12, seed=0):
    x = np.zeros(secs(d))
    for i in range(bursts):
        s = secs(i * spacing)
        n = len(x) - s
        last = i == bursts - 1
        x[s:] += bp(noise(n / SR, seed + i), f_lo, f_hi) * np.exp(-np.arange(n) / SR / (tail if last else 0.005))
    return x


def snare_e(d, tone=200, tone_tau=0.06, noise_tau=0.15, hpf=1500, nlev=1.0, seed=0):
    t = tt(d)
    x = sine(tone * (1 + 0.3 * np.exp(-t / 0.01)), d) * np.exp(-t / tone_tau)
    x += 0.5 * sine(tone * 1.6, d, 1) * np.exp(-t / (tone_tau * 0.6))
    x += hp(noise(d, seed), hpf) * np.exp(-t / noise_tau) * nlev
    return x


def supersaw(f, d, voices=7, detune=22, mix_center=0.35, seed=0):
    r = rng(seed)
    x = np.zeros(secs(d))
    spread = np.linspace(-1, 1, voices)
    for i, s in enumerate(spread):
        fr = f * cents(detune * s + r.uniform(-1.5, 1.5))
        g = mix_center if abs(s) < 1e-6 else (1 - mix_center) / (voices - 1) * 2
        x += saw(fr, d, seed=seed + i) * g
    return x


def chord(fn, root, intervals, d, **kw):
    return sum(fn(mtof(root + iv), d, seed=kw.get("seed", 0) + 10 * k) for k, iv in enumerate(intervals))


def sidechain_free(x):
    return x


MINOR = (0, 3, 7, 12)
MINOR7 = (0, 3, 7, 10)
MINOR9 = (0, 3, 7, 10, 14)


# ================================================================== EDM
EDM = "EDM"


@sound("edm_kick", "Kick EDM", G, EDM, colour=0xffe8833a)
def _():
    d = 0.7
    x = kick(d, 300, 52, 0.022, 0.18, 0.9, 2.8, sustain=0.35, seed=501)
    return finish(x, hpf=28, max_len=0.7)


@sound("edm_clap", "Clap EDM", G, EDM, colour=0xffd8b14a)
def _():
    x = clap(1.0, 4, 0.009, 1100, 3500, 0.14, 502)
    x = mix_at(x, snare_e(0.4, 220, 0.04, 0.08, 2500, 0.4, 503) * 0.5)
    return finish(reverb(x, 0.3, 1.6, 0.45, 8000), max_len=1.2)


@sound("edm_snare", "Snare EDM", G, EDM, colour=0xffd2555a)
def _():
    x = mix_at(snare_e(0.8, 210, 0.08, 0.18, 1800, 1.0, 504), clap(0.8, 3, 0.008, 1200, 4000, 0.12, 505) * 0.7)
    return finish(reverb(drive(x, 1.6), 0.3, 1.8, 0.5, 9000), max_len=1.4)


@sound("edm_hat", "Hi-hat EDM", G, EDM, colour=0xff4fb3a9)
def _():
    d = 0.2; t = tt(d)
    x = hp(noise(d, 506), 9000) * np.exp(-t / 0.03) + hat808(d, 0.03, 1.3, 8000) * 0.6
    return finish(x, max_len=0.2)


@sound("edm_supersaw", "Supersaw Lead", G, EDM, gated=True, release=0.2, colour=0xff3fb6e8)
def _():
    d = 2.5
    x = supersaw(mtof(60), d, 7, 24, seed=507) + 0.5 * supersaw(mtof(72), d, 5, 18, seed=508)
    x = lp(x, 9000) * adsr(d, 0.005, 0.3, 0.85)
    return finish(reverb(x, 0.22, 2.0, 0.6, 9000), max_len=3.0)


@sound("edm_chord", "Accordo Supersaw (minore)", G, EDM, gated=True, release=0.25, colour=0xff35a7da)
def _():
    d = 2.5
    x = chord(lambda f, d, seed: supersaw(f, d, 7, 20, seed=seed), 60, MINOR, d, seed=510)
    x = lp(x, 7000) * adsr(d, 0.005, 0.4, 0.8)
    return finish(reverb(x, 0.25, 2.0, 0.6, 8000), max_len=3.0)


@sound("edm_pluck", "Pluck EDM", G, EDM, colour=0xff4dc7d8)
def _():
    d = 1.0; t = tt(d)
    x = supersaw(mtof(60), d, 3, 12, seed=520)
    x = svf(x, 400 + 7000 * np.exp(-t / 0.09), 0.9) * np.exp(-t / 0.25)
    x = echo(x, 0.32, 0.35, 0.3, 3500, 4)
    return finish(reverb(x, 0.2, 1.4, 0.4), max_len=2.0)


@sound("edm_riser", "Riser", G, EDM, colour=0xff9f8cf0)
def _():
    d = 4.0; t = tt(d)
    p = (t / d) ** 2
    n = svf(noise(d, 521), 300 + 12000 * p, 1.2, "bp")
    s = supersaw(mtof(48) * 2 ** (2 * t / d), d, 5, 30, seed=522) * 0.25
    x = (n + s) * (0.1 + 0.9 * p)
    return finish(reverb(x, 0.3, 2.0, 0.6, 9000, tail=False), max_len=4.0)


@sound("edm_impact", "Impatto (drop)", G, EDM, colour=0xff7d6ad8)
def _():
    d = 3.5; t = tt(d)
    boom = sine(42 + 90 * np.exp(-t / 0.08), d) * np.exp(-t / 0.9)
    crash = hp(noise(d, 523), 2000) * np.exp(-t / 0.6) * 0.3
    x = drive(boom + crash, 1.8)
    return finish(reverb(x, 0.35, 3.5, 1.2, 5000), hpf=22, max_len=4.5)


@sound("edm_vox", "Vocal chop \"Oh\"", G, EDM, root=72, gated=False, colour=0xffe3a5d4)
def _():
    d = 0.45; t = tt(d)
    f = mtof(72) * cents(-40 * np.exp(-t / 0.03))
    src = additive(f, d, lambda k: 1 / k ** 1.05, phase0=1, seed=524)
    x = formants(src, *VOWELS["o"]) * adsr(d, 0.01, 0.2, 0.8, 0.3, 0.05)
    return finish(reverb(x, 0.25, 1.4, 0.4), max_len=1.0)


# ================================================================== House
HOUSE = "House"


@sound("house_kick", "Kick House", G, HOUSE, colour=0xffe8833a)
def _():
    return finish(kick(0.6, 210, 50, 0.03, 0.2, 0.6, 2.0, seed=531), hpf=28, max_len=0.6)


@sound("house_clap", "Clap House", G, HOUSE, colour=0xffd8b14a)
def _():
    return finish(reverb(clap(0.6, 3, 0.011, 800, 2500, 0.1, 532), 0.18, 0.8, 0.2), max_len=0.8)


@sound("house_hat_open", "Hi-hat aperto 909", G, HOUSE, colour=0xff5b8fd6)
def _():
    d = 0.6; t = tt(d)
    x = hat808(d, 0.22, 1.5, 7500) + hp(noise(d, 533), 8000) * np.exp(-t / 0.2) * 0.5
    return finish(x, max_len=0.6)


@sound("house_shaker", "Shaker House", G, HOUSE, colour=0xffbfae6f)
def _():
    d = 0.18; t = tt(d)
    env = (t / 0.012) * np.exp(1 - t / 0.012)
    return finish(bp(noise(d, 534), 5000, 13000) * env, max_len=0.18)


@sound("house_organ", "Organo House", G, HOUSE, root=48, gated=True, release=0.06, colour=0xffc2763b)
def _():
    f = mtof(48); d = 1.0; t = tt(d)
    x = (np.sin(2 * np.pi * f * t) + 0.9 * np.sin(2 * np.pi * f * 2 * t) + 0.6 * np.sin(2 * np.pi * f * 4 * t)
         + 0.35 * np.sin(2 * np.pi * f * 6 * t) * np.exp(-t / 0.1))
    x = drive(x * 0.6, 1.5) * (0.6 * np.exp(-t / 0.3) + 0.4) * adsr(d, 0.002, 0.2, 1.0)
    return finish(x, max_len=1.0)


@sound("house_piano", "Piano House (accordo)", G, HOUSE, gated=True, release=0.25, colour=0xffd9d2c5)
def _():
    from instruments import piano_note
    d = 2.5
    x = sum(piano_note(mtof(60 + iv), d, bright=1.5, seed=540 + i, vel=1.0) for i, iv in enumerate(MINOR7))
    x = peak(hp(x, 150), 3000, 0.8, 5.0)
    return finish(reverb(drive(x * 0.6, 1.3), 0.15, 1.4, 0.4), max_len=3.0)


@sound("house_bass", "Basso House", G, HOUSE, root=36, gated=True, release=0.05, colour=0xff8a6bd1)
def _():
    d = 0.8; t = tt(d)
    x = 0.6 * saw(mtof(36), d, seed=541) + 0.6 * square(mtof(36) * cents(5), d, seed=542)
    x = ladder(x, 250 + 1500 * np.exp(-t / 0.12), 0.35, 1.2) * adsr(d, 0.003, 0.3, 0.7)
    return finish(x, hpf=30, max_len=0.8)


@sound("house_stab", "Stab Detroit", G, HOUSE, gated=False, colour=0xff5bb0c9)
def _():
    d = 1.4; t = tt(d)
    x = chord(lambda f, d, seed: saw(f, d, seed) + saw(f * cents(7), d, seed + 1), 60, MINOR7, d, seed=543)
    x = svf(x, 600 + 4000 * np.exp(-t / 0.06), 0.8) * np.exp(-t / 0.3)
    return finish(reverb(echo(x, 0.36, 0.35, 0.35, 2500, 4), 0.2, 1.4, 0.4), max_len=2.5)


@sound("house_rim", "Rimshot House", G, HOUSE, colour=0xffc94a4f)
def _():
    d = 0.15; t = tt(d)
    x = (sine(1720, d) * 0.6 + sine(455, d)) * np.exp(-t / 0.012) + hp(noise(d, 544), 3000) * np.exp(-t / 0.003)
    return finish(bp(x, 300, 6000), max_len=0.15)


# ================================================================== Techno
TECHNO = "Techno"


@sound("techno_kick", "Kick Techno", G, TECHNO, colour=0xffe8833a)
def _():
    x = kick(0.8, 170, 46, 0.05, 0.32, 0.4, 5.0, seed=551)
    return finish(lp(x, 6000), hpf=28, max_len=0.8)


@sound("techno_rumble", "Rumble Techno", G, TECHNO, colour=0xffd06f30)
def _():
    k = kick(0.6, 160, 46, 0.04, 0.25, 0.3, 4.0, seed=552)
    tail = reverb(k, 1.0, 2.0, 0.7, 2000, seed=553)
    tail = lp(drive(tail * 3, 2.0), 180, order=4)
    t = tt(len(tail) / SR)
    pump = np.clip(t / 0.18, 0, 1) ** 2                   # "ducking" dietro la cassa
    x = mix_at(k, tail * pump * 0.9)
    return finish(x, hpf=28, max_len=1.8)


@sound("techno_hat", "Hi-hat Techno", G, TECHNO, colour=0xff4fb3a9)
def _():
    d = 0.15; t = tt(d)
    x = hat808(d, 0.02, 1.8, 9000) + hp(noise(d, 554), 10000) * np.exp(-t / 0.012) * 0.6
    return finish(x, max_len=0.15)


@sound("techno_ride", "Ride Techno", G, TECHNO, colour=0xff7fa5c9)
def _():
    d = 1.4
    x = metallic(d, 3000, 14000, 60, 0.45, 555) + hat808(d, 0.35, 2.4, 6000) * 0.5
    return finish(hp(x, 3000), max_len=1.4)


@sound("techno_clap", "Clap Techno", G, TECHNO, colour=0xffd8b14a)
def _():
    x = clap(0.4, 3, 0.008, 1000, 3200, 0.05, 556)
    return finish(reverb(x, 0.45, 2.5, 0.8, 5000), max_len=2.0)


@sound("techno_acid", "Acid 303", G, TECHNO, root=48, gated=True, release=0.03, colour=0xffe0d24a)
def _():
    d = 0.5; t = tt(d)
    x = saw(mtof(48), d, seed=557)
    x = ladder(x, 300 + 3200 * np.exp(-t / 0.09), 0.85, 1.8) * adsr(d, 0.002, 0.2, 0.8)
    return finish(drive(x * 2, 1.8), hpf=30, max_len=0.5)


@sound("techno_acid_sq", "Acid 303 (quadra)", G, TECHNO, root=48, gated=True, release=0.03, colour=0xffd6c43f)
def _():
    d = 0.5; t = tt(d)
    x = square(mtof(48), d, seed=558)
    x = ladder(x, 250 + 2500 * np.exp(-t / 0.12), 0.8, 1.6) * adsr(d, 0.002, 0.2, 0.8)
    return finish(drive(x * 2, 1.8), hpf=30, max_len=0.5)


@sound("techno_dubstab", "Stab Dub Techno", G, TECHNO, gated=False, colour=0xff4b9d8e)
def _():
    d = 0.6; t = tt(d)
    x = chord(lambda f, d, seed: saw(f, d, seed) + saw(f * cents(-6), d, seed + 1), 60, MINOR7, d, seed=559)
    x = svf(x, 900 + 1200 * np.exp(-t / 0.05), 0.9) * np.exp(-t / 0.12)
    x = echo(x, 0.36, 0.55, 0.6, 1400, 7)
    return finish(reverb(x, 0.3, 2.0, 0.7, 3000), max_len=3.5)


@sound("techno_perc", "Percussione metallica", G, TECHNO, colour=0xff8fa3ad)
def _():
    d = 0.5; t = tt(d)
    x = fm(420, d, 1.41, 6 * np.exp(-t / 0.05)) * np.exp(-t / 0.12)
    return finish(reverb(hp(x, 300), 0.2, 1.0, 0.3), max_len=0.8)


@sound("techno_noise", "Noise hit", G, TECHNO, colour=0xff9a9a9a)
def _():
    d = 0.5; t = tt(d)
    x = bp(noise(d, 560), 1200, 6000) * np.exp(-t / 0.08)
    return finish(reverb(x, 0.4, 1.8, 0.6, 7000), max_len=1.5)


# ================================================================== Electro
ELECTRO = "Electro"


@sound("electro_kick", "Kick Electro (808)", G, ELECTRO, colour=0xffe8833a)
def _():
    return finish(kick(0.9, 150, 50, 0.04, 0.35, 0.7, 3.5, seed=571), hpf=25, max_len=0.9)


@sound("electro_snare", "Snare Electro (808)", G, ELECTRO, colour=0xffd2555a)
def _():
    return finish(drive(snare_e(0.5, 238, 0.05, 0.12, 2500, 0.9, 572), 1.6), max_len=0.5)


@sound("electro_clap", "Clap Electro", G, ELECTRO, colour=0xffd8b14a)
def _():
    return finish(reverb(clap(0.6, 4, 0.010, 1000, 3000, 0.1, 573), 0.12, 0.7, 0.2), max_len=0.7)


@sound("electro_hat", "Hi-hat 808", G, ELECTRO, colour=0xff4fb3a9)
def _():
    return finish(hat808(0.2, 0.035, 1.0, 7000), max_len=0.2)


@sound("electro_cowbell", "Cowbell 808", G, ELECTRO, colour=0xffd6a24a)
def _():
    d = 0.5; t = tt(d)
    x = (square(540, d, rand_phase=False) + square(800, d, rand_phase=False)) * 0.5
    x = bp(x, 600, 3000) * (0.7 * np.exp(-t / 0.03) + 0.3 * np.exp(-t / 0.2))
    return finish(x, max_len=0.5)


@sound("electro_bass", "Basso Electro", G, ELECTRO, root=36, gated=True, release=0.05, colour=0xff8a6bd1)
def _():
    d = 1.0; t = tt(d)
    f = mtof(36)
    x = saw(f, d, seed=574) + saw(f * cents(14), d, seed=575) + 0.7 * square(f / 2, d, seed=576)
    x = ladder(x, 200 + 2500 * (0.5 + 0.5 * np.sin(2 * np.pi * 3.2 * t - 1.5)), 0.55, 2.0)
    x = drive(x * 2.5, 2.5) * adsr(d, 0.003, 0.3, 0.9)
    return finish(peak(x, 900, 1.0, 4.0), hpf=30, max_len=1.0)


@sound("electro_zap", "Zap laser", G, ELECTRO, colour=0xff52d0c0)
def _():
    d = 0.3; t = tt(d)
    x = sine(80 + 3500 * np.exp(-t / 0.03), d) * np.exp(-t / 0.08)
    return finish(x, max_len=0.3)


@sound("electro_lead", "Lead quadra", G, ELECTRO, root=72, gated=True, release=0.1, colour=0xff3fb6e8)
def _():
    d = 1.8
    f = vibrato(d, mtof(72), 5.5, 15, 0.35, 577)
    x = square(f, d, seed=578) + 0.6 * square(f * cents(8), d, seed=579)
    x = lp(x, 5000) * adsr(d, 0.004, 0.3, 0.85)
    return finish(echo(x, 0.27, 0.3, 0.25, 3000, 3), max_len=2.4)


# ================================================================== Drum and Bass
DNB = "Drum and Bass"


@sound("dnb_kick", "Kick DnB", G, DNB, colour=0xffe8833a)
def _():
    return finish(kick(0.4, 260, 55, 0.012, 0.12, 1.0, 3.5, seed=581, click_hi=9000), hpf=30, max_len=0.4)


@sound("dnb_snare", "Snare DnB", G, DNB, colour=0xffd2555a)
def _():
    d = 0.6; t = tt(d)
    x = snare_e(d, 230, 0.07, 0.12, 2500, 1.0, 582)
    x += hp(noise(d, 583), 5000) * np.exp(-t / 0.006) * 0.8
    return finish(reverb(drive(x, 2.2), 0.12, 0.8, 0.22, 9000), max_len=0.8)


@sound("dnb_hat", "Hi-hat DnB", G, DNB, colour=0xff4fb3a9)
def _():
    d = 0.18; t = tt(d)
    x = hp(noise(d, 584), 7000) * np.exp(-t / 0.025) + metallic(d, 6000, 15000, 20, 0.03, 585) * 0.5
    return finish(x, max_len=0.18)


@sound("dnb_ride", "Ride DnB", G, DNB, colour=0xff7fa5c9)
def _():
    d = 2.0
    x = metallic(d, 2500, 14000, 70, 0.9, 586) + modal(1, d, [3900, 5300], [0.3, 0.2], [0.5, 0.4], seed=587)
    return finish(hp(x, 2500), max_len=2.0)


@sound("dnb_reese", "Reese Bass", G, DNB, root=36, gated=True, release=0.1, colour=0xff6c5ae0)
def _():
    d = 2.5; t = tt(d)
    f = mtof(36)
    x = saw(f * cents(-13), d, seed=588) + saw(f * cents(13), d, seed=589) + 0.5 * saw(f * cents(1), d, seed=590)
    x = ladder(x, 900 + 300 * np.sin(2 * np.pi * 0.4 * t), 0.2, 1.5)
    x = drive(x * 1.6, 1.6) + 0.5 * sine(f, d)
    return finish(x * adsr(d, 0.01, 0.3, 0.9), hpf=28, max_len=2.5)


@sound("dnb_sub", "Sub Bass", G, DNB, root=36, gated=True, release=0.06, colour=0xff5548b8)
def _():
    d = 2.0
    x = sine(mtof(36), d) + 0.08 * sine(mtof(48), d)
    return finish(drive(x, 1.2) * adsr(d, 0.008, 0.3, 0.95), hpf=20, max_len=2.0)


@sound("dnb_neuro", "Neuro stab", G, DNB, root=36, gated=False, colour=0xff8c50d8)
def _():
    d = 0.7; t = tt(d)
    f = mtof(36)
    x = fm(f, d, 0.5, 3 + 4 * np.exp(-t / 0.1)) + 0.6 * saw(f, d, seed=591)
    x = svf(x, 400 + 2500 * (0.5 + 0.5 * np.sin(2 * np.pi * 7 * t)), 2.5, "bp") * 2 + 0.4 * sine(f, d)
    x = drive(x * 2, 3.0) * adsr(d, 0.003, 0.2, 0.7, 0.55, 0.05)
    return finish(x, hpf=30, max_len=0.7)


@sound("dnb_pad", "Pad Liquid", G, DNB, gated=True, release=0.8, colour=0xff63b6d8)
def _():
    d = 4.0
    x = supersaw(mtof(60), d, 5, 14, 0.4, seed=592) + 0.6 * supersaw(mtof(67), d, 5, 14, 0.4, seed=593)
    x = lp(x, 1600) * adsr(d, 0.7, 1.0, 0.85)
    x = chorus(x, 7, 0.4, 0.5)
    return finish(reverb(x, 0.45, 3.0, 1.2, 5000), max_len=5.5)


# ================================================================== Dubstep
DUB = "Dubstep"


@sound("dub_kick", "Kick Dubstep", G, DUB, colour=0xffe8833a)
def _():
    return finish(kick(0.9, 230, 44, 0.045, 0.36, 0.6, 5.0, seed=601), hpf=25, max_len=0.9)


@sound("dub_snare", "Snare Dubstep", G, DUB, colour=0xffd2555a)
def _():
    x = mix_at(snare_e(1.0, 190, 0.1, 0.22, 1200, 1.1, 602), clap(1.0, 4, 0.012, 900, 3000, 0.2, 603) * 0.7)
    return finish(reverb(drive(x, 2.4), 0.35, 2.2, 0.7, 7000), max_len=1.8)


@sound("dub_wobble", "Wobble Bass (140 BPM)", G, DUB, root=36, gated=True, release=0.04, colour=0xff7b4ee0)
def _():
    d = 60 / 140 * 4; t = tt(d)
    f = mtof(36)
    x = saw(f, d, seed=604) + square(f * cents(10), d, seed=605) * 0.7 + 0.6 * sine(f, d)
    lfo = 0.5 - 0.5 * np.cos(2 * np.pi * (140 / 60 * 2) * t)            # un ottavo a 140 BPM
    x = ladder(x, 150 + 3500 * lfo ** 2, 0.7, 2.5)
    x = drive(x * 2.5, 2.5) * adsr(d, 0.003, 0.3, 1.0)
    return finish(x, hpf=28, max_len=d)


@sound("dub_growl", "Growl", G, DUB, root=36, gated=True, release=0.04, colour=0xff9a4ae6)
def _():
    d = 0.9; t = tt(d)
    f = mtof(36)
    src = fm(f, d, 1.0, 4 + 3 * np.sin(2 * np.pi * 3 * t)) + saw(f, d, seed=606)
    # vocale che si apre: "o" -> "a" -> "u"
    a = formants(src, *VOWELS["o"]); b = formants(src, *VOWELS["a"]); c = formants(src, *VOWELS["u"])
    m1 = np.clip(t / 0.3, 0, 1); m2 = np.clip((t - 0.45) / 0.3, 0, 1)
    x = a * (1 - m1) + b * m1 * (1 - m2) + c * m2
    x = drive(x * 4, 3.0) + 0.4 * sine(f, d)
    return finish(x * adsr(d, 0.003, 0.2, 0.9), hpf=30, max_len=0.9)


@sound("dub_sub", "Sub Dubstep", G, DUB, root=36, gated=True, release=0.06, colour=0xff5548b8)
def _():
    d = 1.6
    x = drive(sine(mtof(36), d) * 1.6, 1.8)
    return finish(x * adsr(d, 0.005, 0.3, 0.95), hpf=20, max_len=1.6)


@sound("dub_laser", "Laser", G, DUB, colour=0xff52d0c0)
def _():
    d = 0.5; t = tt(d)
    x = square(200 + 4000 * np.exp(-t / 0.06), d, seed=607) * np.exp(-t / 0.15)
    return finish(echo(lp(x, 8000), 0.11, 0.4, 0.4, 5000, 4), max_len=1.0)


@sound("dub_riser", "Riser Dubstep", G, DUB, colour=0xff9f8cf0)
def _():
    d = 3.0; t = tt(d)
    p = t / d
    x = saw(mtof(36) * 2 ** (3 * p ** 2), d, seed=608) * 0.6 + svf(noise(d, 609), 500 + 10000 * p ** 2, 1.5, "bp")
    x = drive(x * (0.1 + 0.9 * p) * 2, 2.0)
    return finish(x, max_len=3.0)


@sound("dub_impact", "Impatto Dubstep", G, DUB, colour=0xff7d6ad8)
def _():
    d = 3.0; t = tt(d)
    x = sine(38 + 140 * np.exp(-t / 0.06), d) * np.exp(-t / 0.7) + hp(noise(d, 610), 1000) * np.exp(-t / 0.3) * 0.4
    return finish(reverb(drive(x * 2, 2.5), 0.35, 3.0, 1.0, 4000), hpf=22, max_len=4.0)


# ================================================================== Trap
TRAP = "Trap"


@sound("trap_808", "808 Trap", G, TRAP, root=36, gated=True, release=0.08, colour=0xff8a6bd1)
def _():
    d = 2.5; t = tt(d)
    f = mtof(36) * (1 + 0.5 * np.exp(-t / 0.02))
    x = sine(f, d) * (0.8 * np.exp(-t / 1.4) + 0.2)
    x += bp(noise(d, 621), 1500, 6000) * np.exp(-t / 0.003) * 0.4
    return finish(drive(x * 1.8, 2.0), hpf=22, max_len=2.5)


@sound("trap_kick", "Kick Trap", G, TRAP, colour=0xffe8833a)
def _():
    return finish(kick(0.5, 220, 55, 0.02, 0.15, 0.8, 3.0, seed=622), hpf=28, max_len=0.5)


@sound("trap_snare", "Snare Trap", G, TRAP, colour=0xffd2555a)
def _():
    x = snare_e(0.6, 250, 0.04, 0.1, 3000, 1.0, 623)
    return finish(reverb(drive(x, 2.0), 0.25, 1.2, 0.35, 9000), max_len=0.9)


@sound("trap_clap", "Clap Trap", G, TRAP, colour=0xffd8b14a)
def _():
    x = mix_at(clap(0.7, 4, 0.010, 1200, 4500, 0.15, 624), snare_e(0.4, 260, 0.03, 0.06, 4000, 0.4, 625) * 0.4)
    return finish(reverb(x, 0.3, 1.2, 0.35, 9000), max_len=1.0)


@sound("trap_hat", "Hi-hat Trap", G, TRAP, colour=0xff4fb3a9)
def _():
    d = 0.1; t = tt(d)
    x = hat808(d, 0.015, 1.4, 8000) + hp(noise(d, 626), 10000) * np.exp(-t / 0.01) * 0.5
    return finish(x, max_len=0.1)


@sound("trap_hat_open", "Open hat Trap", G, TRAP, colour=0xff5b8fd6)
def _():
    d = 0.5
    return finish(hat808(d, 0.15, 1.4, 7000) + metallic(d, 6000, 14000, 30, 0.15, 627) * 0.4, max_len=0.5)


@sound("trap_rim", "Rim Trap", G, TRAP, colour=0xffc94a4f)
def _():
    d = 0.12; t = tt(d)
    x = sine(1800, d) * np.exp(-t / 0.01) + bp(noise(d, 628), 2000, 7000) * np.exp(-t / 0.004) * 0.6
    return finish(reverb(x, 0.2, 0.8, 0.25), max_len=0.4)


@sound("trap_bell", "Bell Trap", G, TRAP, root=72, gated=False, colour=0xffe6d79a)
def _():
    d = 2.0; t = tt(d)
    x = fm(mtof(72), d, 3.5, 2.5 * np.exp(-t / 0.3)) * np.exp(-t / 0.6)
    return finish(reverb(x, 0.3, 2.0, 0.6), max_len=2.5)


@sound("trap_flute", "Flauto Trap", G, TRAP, root=72, gated=True, release=0.15, colour=0xffa7d4e8)
def _():
    d = 1.8; t = tt(d)
    f = vibrato(d, mtof(72), 5.5, 22, 0.2, 629)
    x = sine(f, d) + 0.25 * sine(f * 2, d) + bp(noise(d, 630), 1000, 5000) * 0.15
    x = drive(x * adsr(d, 0.03, 0.3, 0.8), 1.4)
    return finish(reverb(x, 0.4, 2.0, 0.6), max_len=2.5)


# ================================================================== Synth e pad
SYN = "Synth e pad"


@sound("syn_pad_warm", "Pad caldo", G, SYN, gated=True, release=0.9, colour=0xff63b6d8)
def _():
    d = 4.5
    x = supersaw(mtof(60), d, 7, 12, 0.3, seed=641)
    x = lp(x, 1200, order=4) * adsr(d, 0.8, 1.0, 0.9)
    return finish(reverb(chorus(x, 8, 0.3, 0.5), 0.4, 3.0, 1.0, 5000), max_len=6.0)


@sound("syn_pad_glass", "Pad di vetro", G, SYN, gated=True, release=1.0, colour=0xff9fd6e6)
def _():
    d = 4.5; t = tt(d)
    f = mtof(60)
    x = fm(f, d, 2.0, 1.5 + 0.5 * np.sin(2 * np.pi * 0.3 * t)) + 0.4 * fm(f * 2, d, 3.5, 0.8)
    x = chorus(x * adsr(d, 0.9, 1.0, 0.9), 6, 0.25, 0.5)
    return finish(reverb(x, 0.5, 3.5, 1.4, 7000), max_len=6.0)


@sound("syn_pad_dark", "Pad scuro", G, SYN, root=48, gated=True, release=1.0, colour=0xff4f6f9a)
def _():
    d = 4.5; t = tt(d)
    x = supersaw(mtof(48), d, 5, 10, 0.4, seed=642) + 0.4 * square(mtof(48) * cents(-6), d, seed=643)
    x = svf(x, 500 + 300 * np.sin(2 * np.pi * 0.15 * t), 1.2) * adsr(d, 1.0, 1.0, 0.9)
    return finish(reverb(x, 0.45, 3.5, 1.2, 3000), max_len=6.0)


@sound("syn_lead_saw", "Lead saw", G, SYN, gated=True, release=0.12, colour=0xff3fb6e8)
def _():
    d = 2.0
    f = vibrato(d, mtof(60), 5.5, 12, 0.4, 644)
    x = saw(f, d, seed=645) + saw(f * cents(9), d, seed=646)
    x = lp(x, 6000) * adsr(d, 0.004, 0.3, 0.85)
    return finish(echo(x, 0.3, 0.3, 0.2, 4000, 3), max_len=2.6)


@sound("syn_lead_soft", "Lead morbido", G, SYN, root=72, gated=True, release=0.15, colour=0xff6dc1e3)
def _():
    d = 2.0
    f = vibrato(d, mtof(72), 5.0, 10, 0.4, 647)
    x = triangle(f, d) + 0.3 * sine(f * 2, d)
    x *= adsr(d, 0.03, 0.3, 0.85)
    return finish(reverb(x, 0.25, 1.6, 0.5), max_len=2.6)


@sound("syn_pluck", "Pluck", G, SYN, colour=0xff4dc7d8)
def _():
    d = 0.9; t = tt(d)
    x = saw(mtof(60), d, seed=648) + square(mtof(60) * cents(7), d, seed=649) * 0.5
    x = svf(x, 300 + 5000 * np.exp(-t / 0.06), 1.0) * np.exp(-t / 0.2)
    return finish(reverb(x, 0.2, 1.2, 0.35), max_len=1.2)


@sound("syn_arp", "Arp FM", G, SYN, root=72, colour=0xff5fd2c4)
def _():
    d = 0.5; t = tt(d)
    x = fm(mtof(72), d, 2.0, 3 * np.exp(-t / 0.05)) * np.exp(-t / 0.1)
    return finish(x, max_len=0.5)


@sound("syn_bell", "Campana FM", G, SYN, root=72, gated=False, colour=0xffe6d79a)
def _():
    d = 3.5; t = tt(d)
    f = mtof(72)
    x = fm(f, d, 3.5, 3 * np.exp(-t / 0.6)) * np.exp(-t / 1.2) + 0.3 * fm(f, d, 1.0, 1.0) * np.exp(-t / 0.4)
    return finish(reverb(x, 0.3, 2.5, 0.8), max_len=4.0)


@sound("syn_sub", "Sub sinusoide", G, SYN, root=36, gated=True, release=0.05, colour=0xff5548b8)
def _():
    d = 2.0
    return finish(sine(mtof(36), d) * adsr(d, 0.005, 0.2, 1.0), hpf=18, max_len=2.0)


@sound("syn_bass_moog", "Basso analogico", G, SYN, root=36, gated=True, release=0.06, colour=0xff7a5ad0)
def _():
    d = 1.2; t = tt(d)
    f = mtof(36)
    x = saw(f, d, seed=650) + saw(f * cents(-7), d, seed=651) + square(f / 2, d, seed=652) * 0.6
    x = ladder(x, 120 + 1400 * np.exp(-t / 0.15), 0.45, 1.5) * adsr(d, 0.003, 0.4, 0.7)
    return finish(x, hpf=28, max_len=1.2)


@sound("syn_rave_stab", "Stab rave", G, SYN, gated=False, colour=0xff3aa0f0)
def _():
    d = 1.0; t = tt(d)
    pitch = cents(-120 * np.exp(-t / 0.04))
    x = sum(supersaw(mtof(60 + iv) * pitch, d, 5, 25, seed=653 + iv) for iv in MINOR)
    x = drive(lp(x, 6000) * np.exp(-t / 0.3), 1.5)
    return finish(reverb(x, 0.25, 1.4, 0.4), max_len=1.6)


@sound("syn_hoover", "Hoover", G, SYN, root=48, gated=True, release=0.1, colour=0xff7d8cf0)
def _():
    d = 2.0; t = tt(d)
    f = mtof(48) * cents(-200 * np.exp(-t / 0.08))      # il classico "lamento" in salita
    x = sum(pulse(f * cents(c), d, 0.2, seed=660 + i) for i, c in enumerate((-20, -8, 0, 8, 20)))
    x += 0.6 * pulse(f / 2, d, 0.3, seed=670)
    x = chorus(lp(x, 4500), 8, 0.6, 0.6)
    return finish(drive(x * 0.8, 1.5) * adsr(d, 0.01, 0.4, 0.85), max_len=2.0)


# ================================================================== FX
FX = "Effetti (FX)"


@sound("fx_riser_noise", "Riser di rumore", G, FX, colour=0xff9f8cf0)
def _():
    d = 4.0; t = tt(d)
    p = t / d
    x = svf(noise(d, 701), 200 + 14000 * p ** 2, 1.4, "bp") * (0.05 + 0.95 * p ** 1.5)
    return finish(x, max_len=4.0)


@sound("fx_downlifter", "Downlifter", G, FX, colour=0xff8a7ad8)
def _():
    d = 2.5; t = tt(d)
    p = t / d
    x = svf(noise(d, 702), 12000 * (1 - p) ** 2 + 150, 1.2, "bp") * (1 - p) ** 1.2
    return finish(reverb(x, 0.3, 2.0, 0.6, tail=False), max_len=2.5)


@sound("fx_uplifter", "Uplifter tonale", G, FX, colour=0xffa79cf2)
def _():
    d = 3.0; t = tt(d)
    p = t / d
    x = supersaw(mtof(48) * 2 ** (2 * p), d, 5, 30, seed=703) * p ** 1.5
    return finish(reverb(lp(x, 9000), 0.3, 2.0, 0.6, tail=False), max_len=3.0)


@sound("fx_reverse_cymbal", "Piatto al contrario", G, FX, colour=0xff6b9bc3)
def _():
    d = 2.5
    from instruments import cymbal
    x = cymbal(d, 800, 16000, 100, 0.9, 704, 0.8, 1500)
    x = x[::-1].copy()
    return finish(x, max_len=2.5)


@sound("fx_impact", "Impatto", G, FX, colour=0xff7d6ad8)
def _():
    d = 3.0; t = tt(d)
    x = sine(45 + 100 * np.exp(-t / 0.05), d) * np.exp(-t / 0.8) + hp(noise(d, 705), 800) * np.exp(-t / 0.15) * 0.5
    return finish(reverb(x, 0.4, 3.0, 1.0, 5000), hpf=22, max_len=4.0)


@sound("fx_sweep", "Sweep", G, FX, colour=0xff8d8db0)
def _():
    d = 2.0; t = tt(d)
    x = svf(noise(d, 706), 300 * 2 ** (6 * np.sin(np.pi * t / d)), 2.0, "bp") * np.sin(np.pi * t / d)
    return finish(x, max_len=2.0)


@sound("fx_zap", "Zap", G, FX, colour=0xff52d0c0)
def _():
    d = 0.25; t = tt(d)
    return finish(sine(120 + 2500 * np.exp(-t / 0.02), d) * np.exp(-t / 0.06), max_len=0.25)


@sound("fx_vinyl", "Fruscio vinile", G, FX, colour=0xff8b7d6b)
def _():
    d = 3.0
    r = rng(707)
    x = lp(noise(d, 708), 3000) * 0.08
    for _ in range(60):
        i = r.integers(0, secs(d) - 200)
        x[i:i + 60] += hp(r.uniform(-1, 1, 60), 2000) * r.uniform(0.2, 1.0) * np.hanning(60)
    return finish(bp(x, 300, 8000), max_len=3.0, peak=0.6)


@sound("fx_tape_stop", "Tape stop", G, FX, colour=0xff9c8a6c)
def _():
    d = 1.2; t = tt(d)
    f = mtof(48) * np.clip(1 - t / d, 0.02, 1) ** 1.5
    x = supersaw(f, d, 3, 10, seed=709) + 0.6 * sine(f / 2, d)
    return finish(lp(x, 4000) * np.clip(1 - t / d, 0, 1), max_len=1.2)
