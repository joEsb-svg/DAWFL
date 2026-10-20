"""Suoni in stile drum machine classiche (808, 909) e kit Lo-Fi, per i kit di DrumForge."""
import numpy as np
from dsp import *
from registry import sound
from electronic import kick, clap, snare_e

G = "Elettronica"
DM = "Drum machine"


def bitcrush(x, rate=12000, bits=8):
    """Riduce frequenza di campionamento e risoluzione (suono "Lo-Fi")."""
    step = max(1, int(round(SR / rate)))
    held = np.repeat(x[::step], step)[: len(x)]
    q = 2 ** (bits - 1)
    return np.round(held * q) / q


def lofi(x, seed=0, noise_amt=0.01):
    x = lp(bitcrush(x * 0.9, 14000, 9), 7000)
    return x + lp(noise(len(x) / SR, seed), 4000) * noise_amt


# ------------------------------------------------------------------ 808
@sound("d808_kick", "808 Kick", G, DM, colour=0xffe8833a)
def _():
    d = 1.4; t = tt(d)
    f = 50 + 45 * np.exp(-t / 0.035)
    x = sine(f, d) * np.exp(-t / 0.55)
    x += bp(noise(d, 801), 800, 4000) * np.exp(-t / 0.002) * 0.2
    return finish(drive(x, 1.3), hpf=20, max_len=1.4)


@sound("d808_snare", "808 Snare", G, DM, colour=0xffd2555a)
def _():
    d = 0.45; t = tt(d)
    x = (sine(238, d) * 0.7 + sine(476, d) * 0.45) * np.exp(-t / 0.06)
    x += hp(noise(d, 802), 1800) * np.exp(-t / 0.09) * 0.75
    return finish(x, max_len=0.45)


@sound("d808_clap", "808 Clap", G, DM, colour=0xffd8b14a)
def _():
    return finish(reverb(clap(0.6, 4, 0.011, 800, 2400, 0.13, 803), 0.12, 0.6, 0.15), max_len=0.7)


@sound("d808_ch", "808 Hi-hat chiuso", G, DM, colour=0xff4fb3a9)
def _():
    return finish(hat808(0.15, 0.03, 1.0, 7000), max_len=0.15)


@sound("d808_oh", "808 Hi-hat aperto", G, DM, colour=0xff5b8fd6)
def _():
    return finish(hat808(0.8, 0.28, 1.0, 7000), max_len=0.8)


def tom808(f, seed):
    d = 1.0; t = tt(d)
    x = sine(f * (1 + 0.25 * np.exp(-t / 0.04)), d) * np.exp(-t / 0.28)
    x += hp(noise(d, seed), 2000) * np.exp(-t / 0.004) * 0.15
    return finish(x, max_len=1.0)


@sound("d808_tom_lo", "808 Tom basso", G, DM, colour=0xff9a78d0)
def _():
    return tom808(90, 804)


@sound("d808_tom_mid", "808 Tom medio", G, DM, colour=0xff8d6cc2)
def _():
    return tom808(135, 805)


@sound("d808_tom_hi", "808 Tom alto", G, DM, colour=0xff7f60b3)
def _():
    return tom808(200, 806)


@sound("d808_cowbell", "808 Cowbell", G, DM, colour=0xffd6a24a)
def _():
    d = 0.5; t = tt(d)
    x = (square(540, d, rand_phase=False) + square(800, d, rand_phase=False)) * 0.5
    x = bp(x, 600, 3000) * (0.7 * np.exp(-t / 0.03) + 0.3 * np.exp(-t / 0.18))
    return finish(x, max_len=0.5)


@sound("d808_rim", "808 Rimshot", G, DM, colour=0xffc94a4f)
def _():
    d = 0.08; t = tt(d)
    x = (sine(1720, d) * 0.6 + sine(455, d)) * np.exp(-t / 0.008)
    return finish(bp(x, 300, 6000), max_len=0.08)


@sound("d808_clave", "808 Clave", G, DM, colour=0xffd8b989)
def _():
    d = 0.12; t = tt(d)
    return finish(sine(2500, d) * np.exp(-t / 0.02), max_len=0.12)


@sound("d808_cymbal", "808 Piatto", G, DM, colour=0xff6b9bc3)
def _():
    d = 2.0
    x = hat808(d, 0.8, 0.8, 4000) + hp(noise(d, 807), 6000) * expenv(d, 0.6) * 0.3
    return finish(x, max_len=2.0)


@sound("d808_conga_lo", "808 Conga bassa", G, DM, colour=0xffa87d4c)
def _():
    d = 0.5; t = tt(d)
    return finish(sine(220 * (1 + 0.1 * np.exp(-t / 0.01)), d) * np.exp(-t / 0.12), max_len=0.5)


@sound("d808_conga_hi", "808 Conga alta", G, DM, colour=0xffb98a55)
def _():
    d = 0.4; t = tt(d)
    return finish(sine(330 * (1 + 0.1 * np.exp(-t / 0.01)), d) * np.exp(-t / 0.1), max_len=0.4)


@sound("d808_maracas", "808 Maracas", G, DM, colour=0xffbfae6f)
def _():
    d = 0.06; t = tt(d)
    return finish(hp(noise(d, 808), 7000) * np.exp(-t / 0.012), max_len=0.06)


# ------------------------------------------------------------------ 909
@sound("d909_kick", "909 Kick", G, DM, colour=0xffe8833a)
def _():
    d = 0.6; t = tt(d)
    f = 52 + 180 * np.exp(-t / 0.012) + 40 * np.exp(-t / 0.06)
    x = sine(f, d) * np.exp(-t / 0.22)
    x += bp(noise(d, 901), 2500, 9000) * np.exp(-t / 0.004) * 0.55   # il "click" del 909
    return finish(drive(x, 2.0), hpf=25, max_len=0.6)


@sound("d909_snare", "909 Snare", G, DM, colour=0xffd2555a)
def _():
    d = 0.5; t = tt(d)
    x = (sine(185 * (1 + 0.4 * np.exp(-t / 0.008)), d) + 0.5 * sine(330, d)) * np.exp(-t / 0.05)
    x += hp(noise(d, 902), 1200) * np.exp(-t / 0.13) * 1.1
    return finish(drive(x, 1.5), max_len=0.5)


@sound("d909_clap", "909 Clap", G, DM, colour=0xffd8b14a)
def _():
    return finish(reverb(clap(0.7, 4, 0.009, 1000, 3200, 0.16, 903), 0.15, 0.7, 0.18), max_len=0.8)


@sound("d909_ch", "909 Hi-hat chiuso", G, DM, colour=0xff4fb3a9)
def _():
    d = 0.12; t = tt(d)
    x = metallic(d, 6000, 15000, 40, 0.03, 904) + hp(noise(d, 905), 8000) * np.exp(-t / 0.02) * 0.6
    return finish(hp(x, 6000), max_len=0.12)


@sound("d909_oh", "909 Hi-hat aperto", G, DM, colour=0xff5b8fd6)
def _():
    d = 0.7; t = tt(d)
    x = metallic(d, 5000, 15000, 50, 0.25, 906) + hp(noise(d, 907), 7000) * np.exp(-t / 0.2) * 0.5
    return finish(hp(x, 5000), max_len=0.7)


def tom909(f, seed):
    d = 0.8; t = tt(d)
    x = sine(f * (1 + 0.6 * np.exp(-t / 0.02)), d) * np.exp(-t / 0.22)
    x += bp(noise(d, seed), 400, 3000) * np.exp(-t / 0.03) * 0.3
    return finish(drive(x, 1.4), max_len=0.8)


@sound("d909_tom_lo", "909 Tom basso", G, DM, colour=0xff9a78d0)
def _():
    return tom909(95, 908)


@sound("d909_tom_mid", "909 Tom medio", G, DM, colour=0xff8d6cc2)
def _():
    return tom909(140, 909)


@sound("d909_tom_hi", "909 Tom alto", G, DM, colour=0xff7f60b3)
def _():
    return tom909(210, 910)


@sound("d909_rim", "909 Rimshot", G, DM, colour=0xffc94a4f)
def _():
    d = 0.1; t = tt(d)
    x = (sine(1650, d) * 0.5 + sine(480, d)) * np.exp(-t / 0.01) + hp(noise(d, 911), 3000) * np.exp(-t / 0.003)
    return finish(x, max_len=0.1)


@sound("d909_crash", "909 Crash", G, DM, colour=0xff6b9bc3)
def _():
    d = 2.5
    return finish(hp(metallic(d, 2000, 15000, 100, 0.9, 912) + hp(noise(d, 913), 4000) * expenv(d, 0.7) * 0.6, 2500), max_len=2.5)


@sound("d909_ride", "909 Ride", G, DM, colour=0xff7fa5c9)
def _():
    d = 2.0
    return finish(hp(metallic(d, 3000, 14000, 80, 0.8, 914), 3000), max_len=2.0)


# ------------------------------------------------------------------ Lo-Fi
@sound("lofi_kick", "Lo-Fi Kick", G, DM, colour=0xffe8833a)
def _():
    x = kick(0.5, 140, 52, 0.03, 0.15, 0.4, 1.8, seed=921)
    return finish(lofi(lp(x, 3000), 922), hpf=30, max_len=0.5)


@sound("lofi_snare", "Lo-Fi Snare", G, DM, colour=0xffd2555a)
def _():
    x = snare_e(0.4, 200, 0.05, 0.1, 1500, 0.8, 923)
    return finish(lofi(lp(x, 5000), 924, 0.02), max_len=0.4)


@sound("lofi_hat", "Lo-Fi Hi-hat", G, DM, colour=0xff4fb3a9)
def _():
    d = 0.1; t = tt(d)
    x = hp(noise(d, 925), 5000) * np.exp(-t / 0.02)
    return finish(lofi(x, 926), max_len=0.1)


@sound("lofi_rim", "Lo-Fi Rim", G, DM, colour=0xffc94a4f)
def _():
    d = 0.12; t = tt(d)
    x = sine(1200, d) * np.exp(-t / 0.012) + bp(noise(d, 927), 1000, 4000) * np.exp(-t / 0.006)
    return finish(lofi(x, 928), max_len=0.12)


@sound("lofi_shaker", "Lo-Fi Shaker", G, DM, colour=0xffbfae6f)
def _():
    d = 0.18; t = tt(d)
    env = (t / 0.015) * np.exp(1 - t / 0.015)
    return finish(lofi(bp(noise(d, 929), 3000, 9000) * env, 930), max_len=0.18)
