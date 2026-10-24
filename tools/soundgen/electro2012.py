"""
Libreria "Electro": suoni nello stile complextro / electro house aggressiva 2008-2012 / fidget house / brostep.

Ogni suono esce gia' "da produzione": equalizzato per la sua funzione (niente fango sotto i 300 Hz dove non
serve, attacco in evidenza, acuti puliti), compresso dove serve e portato a un volume coerente con gli altri
della stessa famiglia (polish()).

Gli id iniziano con "x_" e non vanno mai cambiati (sono salvati nei progetti).
"""
import numpy as np
from dsp import *
from registry import sound

DRUMS = "Batteria"
INSTR = "Strumenti"


# ============================================================================ finitura per famiglia

def compress(x, thr_db=-18.0, ratio=4.0, att=0.003, rel=0.08, makeup_db=0.0):
    """Compressore semplice (rilevatore di picco con attacco/rilascio), vettoriale."""
    env = np.abs(x)
    a = np.exp(-1.0 / (att * SR))
    r = np.exp(-1.0 / (rel * SR))
    # inseguitore con attacco e rilascio diversi (due passaggi approssimati con lfilter)
    up = sg.lfilter([1 - a], [1, -a], env)
    follow = np.maximum(up, sg.lfilter([1 - r], [1, -r], env))
    lvl = 20 * np.log10(np.maximum(follow, 1e-6))
    over = np.maximum(0.0, lvl - thr_db)
    gain_db = -over * (1 - 1 / ratio) + makeup_db
    return x * 10 ** (gain_db / 20)


def soft_limit(x, ceiling=0.89):
    """Porta i picchi sotto il tetto con una curva morbida (niente click di clip)."""
    k = 0.75 * ceiling
    y = x.copy()
    over = np.abs(y) > k
    s = np.sign(y[over])
    v = np.abs(y[over]) - k
    y[over] = s * (k + (ceiling - k) * np.tanh(v / (ceiling - k)))
    return y


def loudness(x, win=0.05):
    """Livello "percepito" grezzo: l'RMS della finestra piu' forte (50 ms)."""
    n = max(1, secs(win))
    if len(x) < n:
        return np.sqrt(np.mean(x ** 2) + 1e-12)
    c = np.cumsum(np.concatenate([[0.0], x ** 2]))
    rms = np.sqrt((c[n:] - c[:-n]) / n)
    return float(rms.max() + 1e-12)


# Per ogni famiglia: (passa-alto Hz, lista di campane (Hz, Q, dB), passa-basso Hz, compressione, livello)
FAMILY = {
    "kick":  (28,  [(320, 1.0, -4.0), (4200, 1.2, 3.0)],               None,  (-14, 3.0, 0.002, 0.06, 2.0), 0.42),
    "snare": (120, [(450, 1.2, -3.5), (5200, 0.9, 2.5)],               17000, (-16, 3.5, 0.001, 0.08, 2.0), 0.34),
    "clap":  (300, [(800, 1.0, -2.0), (2400, 0.8, 2.0)],               15000, (-18, 3.0, 0.001, 0.06, 2.0), 0.30),
    "hat":   (600, [(9500, 1.5, -2.5)],                                 17500, None,                         0.17),
    "ohat":  (500, [(9000, 1.5, -2.5)],                                 17000, None,                         0.16),
    "cym":   (350, [(7000, 1.0, -2.0)],                                 16000, None,                         0.15),
    "tom":   (60,  [(380, 1.2, -3.0), (3500, 1.0, 2.0)],               14000, (-16, 3.0, 0.002, 0.08, 1.5), 0.32),
    "perc":  (150, [],                                                   16000, (-16, 2.5, 0.001, 0.05, 1.0), 0.24),
    "bass":  (30,  [(220, 1.0, -1.5), (1500, 1.0, 1.5)],               14000, (-16, 3.0, 0.005, 0.12, 2.0), 0.30),
    "synth": (110, [(350, 1.0, -2.5), (3000, 0.9, 1.5)],               16000, (-16, 2.5, 0.005, 0.12, 1.5), 0.24),
    "fx":    (40,  [],                                                   17000, None,                         0.24),
    "vox":   (150, [(350, 1.0, -2.0), (3200, 1.0, 2.5)],               14000, (-18, 3.0, 0.003, 0.08, 2.0), 0.24),
}


def polish(x, family, max_len=None, mono_below=True):
    hpf, bells, lpf, comp, target = FAMILY[family]
    x = np.asarray(x, dtype=float)
    if max_len is not None:
        x = x[: secs(max_len)]
    x = hp(x, hpf, order=2)
    for f, q, g in bells:
        x = peak(x, f, q, g)
    if lpf:
        x = lp(x, lpf, order=2)
    if comp:
        x = compress(x, *comp)
    x = x * (target / loudness(x))
    x = soft_limit(x, 0.89)
    # coda: taglio dove il suono e' ormai inudibile, e sfumatura breve
    m = np.max(np.abs(x)) + 1e-12
    above = np.nonzero(np.abs(x) > m * 10 ** (-54 / 20))[0]
    end = min(len(x), (above[-1] + secs(0.015)) if len(above) else len(x))
    x = fade(x[:end], 0.0, min(0.02, end / SR * 0.2))
    return x


def pitch_env(d, start, end, tau):
    return end + (start - end) * np.exp(-tt(d) / tau)


def click(d, seed, lo=2000, hi=9000, tau=0.0015, amt=0.5):
    return bp(noise(d, seed), lo, hi) * np.exp(-tt(d) / tau) * amt


# ============================================================================ KICK
DRUM_COL = {"kick": 0xffe8833a, "snare": 0xffd2555a, "clap": 0xffd8b14a, "hat": 0xff4fb3a9, "ohat": 0xff5b8fd6,
            "cym": 0xff7f9fd0, "tom": 0xffb06cc4, "perc": 0xff57b98a}


def kick_x(d, f0, f1, ptau, btau, clk, dist, seed, tok=0.0, sub=0.0, tail_tau=None):
    t = tt(d)
    f = pitch_env(d, f0, f1, ptau)
    body = np.sin(phase_of(f)) * np.exp(-t / btau)
    if tail_tau:
        body += np.sin(phase_of(np.full(len(t), f1))) * (1 - np.exp(-t / 0.02)) * np.exp(-t / tail_tau) * sub
    x = body + click(d, seed, 1800, 9000, 0.0018, clk)
    if tok:
        x += np.sin(2 * np.pi * 1100 * t) * np.exp(-t / 0.004) * tok
    return drive(x, dist) if dist > 1.0 else x


KICKS = [
    ("x_kick_complextro", "Kick Complextro",   dict(d=0.45, f0=260, f1=52, ptau=0.022, btau=0.16, clk=0.7, dist=2.6, seed=11, tok=0.25)),
    ("x_kick_punch",      "Kick Electro Punch", dict(d=0.40, f0=220, f1=55, ptau=0.025, btau=0.14, clk=0.5, dist=1.8, seed=12, tok=0.35)),
    ("x_kick_dutch",      "Kick Dutch",         dict(d=0.55, f0=300, f1=50, ptau=0.018, btau=0.20, clk=0.6, dist=3.2, seed=13, tok=0.2)),
    ("x_kick_fidget",     "Kick Fidget",        dict(d=0.32, f0=200, f1=58, ptau=0.020, btau=0.10, clk=0.8, dist=1.6, seed=14, tok=0.4)),
    ("x_kick_brostep",    "Kick Brostep",       dict(d=0.60, f0=280, f1=46, ptau=0.030, btau=0.22, clk=0.9, dist=2.2, seed=15, tok=0.3, sub=0.4, tail_tau=0.25)),
    ("x_kick_dist",       "Kick Distorto",      dict(d=0.50, f0=240, f1=50, ptau=0.028, btau=0.18, clk=0.5, dist=4.5, seed=16)),
    ("x_kick_tight",      "Kick Secco",         dict(d=0.25, f0=180, f1=60, ptau=0.015, btau=0.07, clk=0.6, dist=1.4, seed=17, tok=0.3)),
    ("x_kick_sub",        "Kick Sub",           dict(d=0.80, f0=160, f1=45, ptau=0.035, btau=0.18, clk=0.3, dist=1.3, seed=18, sub=0.7, tail_tau=0.35)),
    ("x_kick_clicky",     "Kick Click",         dict(d=0.35, f0=230, f1=56, ptau=0.020, btau=0.12, clk=1.2, dist=1.7, seed=19, tok=0.5)),
    ("x_kick_hardelectro","Kick Electro Duro",  dict(d=0.45, f0=320, f1=54, ptau=0.016, btau=0.15, clk=0.8, dist=3.6, seed=20, tok=0.25)),
    ("x_kick_bigroom",    "Kick Big Room",      dict(d=0.70, f0=250, f1=48, ptau=0.032, btau=0.26, clk=0.6, dist=2.4, seed=21, tok=0.2, sub=0.3, tail_tau=0.3)),
    ("x_kick_crunch",     "Kick Crunch",        dict(d=0.40, f0=210, f1=53, ptau=0.024, btau=0.13, clk=0.7, dist=5.5, seed=22)),
]

for sid, name, p in KICKS:
    def _make(p=p):
        return lambda: polish(kick_x(**p), "kick")
    sound(sid, name, DRUMS, "Kick", colour=DRUM_COL["kick"])(_make())


# ============================================================================ SNARE
def snare_x(d, tone, drop, ttau, ntau, n_lo, n_hi, crack, dist, seed, room=0.0, clap_layer=0.0):
    t = tt(d)
    f = pitch_env(d, tone * (1 + drop), tone, 0.012)
    body = (np.sin(phase_of(f)) + 0.45 * np.sin(phase_of(f * 1.88))) * np.exp(-t / ttau)
    nz = bp(noise(d, seed), n_lo, n_hi) * np.exp(-t / ntau)
    x = body * 0.8 + nz + click(d, seed + 1, 3000, 10000, 0.001, crack)
    if clap_layer:
        x += clap_x(d, 3, 0.009, 0.08, 1100, 2600, seed + 7) * clap_layer
    if dist > 1.0:
        x = drive(x, dist)
    if room:
        x = reverb(x, room, 0.45, 0.12, 7000, seed=seed)
    return x


def clap_x(d, bursts, spacing, tail, lo, hi, seed, room=0.0):
    t = tt(d)
    out = np.zeros(len(t))
    for b in range(bursts):
        s = secs(b * spacing)
        last = b == bursts - 1
        n = len(t) - s
        env = np.exp(-np.arange(n) / SR / (tail if last else 0.004))
        out[s:] += bp(noise(n / SR, seed + b), lo, hi) * env * (1.0 if last else 0.8)
    if room:
        out = reverb(out, room, 0.5, 0.12, 6000, seed=seed)
    return out


SNARES = [
    ("x_snare_complextro", "Snare Complextro",  dict(d=0.40, tone=210, drop=0.5, ttau=0.05, ntau=0.11, n_lo=1500, n_hi=9000, crack=0.8, dist=2.4, seed=31, room=0.1, clap_layer=0.4)),
    ("x_snare_crack",      "Snare Electro Crack", dict(d=0.35, tone=240, drop=0.4, ttau=0.04, ntau=0.09, n_lo=2000, n_hi=11000, crack=1.2, dist=1.8, seed=32, room=0.08)),
    ("x_snare_brostep",    "Snare Brostep",     dict(d=0.55, tone=180, drop=0.7, ttau=0.07, ntau=0.16, n_lo=1200, n_hi=8000, crack=0.9, dist=2.8, seed=33, room=0.18, clap_layer=0.5)),
    ("x_snare_fidget",     "Snare Fidget",      dict(d=0.30, tone=260, drop=0.3, ttau=0.035, ntau=0.07, n_lo=2200, n_hi=10000, crack=1.0, dist=1.5, seed=34, room=0.06)),
    ("x_snare_dirty",      "Snare Sporco",      dict(d=0.40, tone=200, drop=0.5, ttau=0.05, ntau=0.12, n_lo=1000, n_hi=7000, crack=0.6, dist=4.5, seed=35, room=0.08)),
    ("x_snare_tight",      "Snare Secco",       dict(d=0.25, tone=250, drop=0.3, ttau=0.03, ntau=0.06, n_lo=2500, n_hi=11000, crack=0.9, dist=1.4, seed=36)),
    ("x_snare_rim",        "Snare Rim",         dict(d=0.25, tone=330, drop=0.2, ttau=0.025, ntau=0.05, n_lo=3000, n_hi=12000, crack=1.4, dist=1.6, seed=37)),
    ("x_snare_layered",    "Snare + Clap",      dict(d=0.40, tone=220, drop=0.4, ttau=0.045, ntau=0.10, n_lo=1800, n_hi=9500, crack=0.8, dist=2.0, seed=38, room=0.1, clap_layer=0.8)),
    ("x_snare_dist",       "Snare Distorta",    dict(d=0.40, tone=190, drop=0.6, ttau=0.06, ntau=0.12, n_lo=1400, n_hi=8500, crack=0.7, dist=6.0, seed=39, room=0.06)),
    ("x_snare_bigroom",    "Snare Big Room",    dict(d=0.60, tone=200, drop=0.5, ttau=0.06, ntau=0.18, n_lo=1500, n_hi=9000, crack=0.8, dist=2.0, seed=40, room=0.22, clap_layer=0.6)),
]
for sid, name, p in SNARES:
    def _make(p=p):
        return lambda: polish(snare_x(**p), "snare")
    sound(sid, name, DRUMS, "Rullanti", colour=DRUM_COL["snare"])(_make())


# ============================================================================ CLAP
CLAPS = [
    ("x_clap_electro", "Clap Electro",   dict(d=0.45, bursts=4, spacing=0.010, tail=0.11, lo=900, hi=2800, seed=51, room=0.1)),
    ("x_clap_fidget",  "Clap Fidget",    dict(d=0.30, bursts=3, spacing=0.008, tail=0.06, lo=1200, hi=3500, seed=52, room=0.05)),
    ("x_clap_thick",   "Clap Pieno",     dict(d=0.50, bursts=5, spacing=0.011, tail=0.14, lo=700, hi=2400, seed=53, room=0.12)),
    ("x_clap_snappy",  "Clap Scattante", dict(d=0.30, bursts=3, spacing=0.007, tail=0.05, lo=1500, hi=4500, seed=54)),
    ("x_clap_crunch",  "Clap Crunch",    dict(d=0.40, bursts=4, spacing=0.009, tail=0.09, lo=1000, hi=3200, seed=55, room=0.06)),
    ("x_clap_wide",    "Clap Largo",     dict(d=0.60, bursts=4, spacing=0.013, tail=0.16, lo=800, hi=2600, seed=56, room=0.2)),
    ("x_clap_dutch",   "Clap Dutch",     dict(d=0.40, bursts=4, spacing=0.010, tail=0.10, lo=1100, hi=3000, seed=57, room=0.08)),
    ("x_clap_tight",   "Clap Corto",     dict(d=0.22, bursts=3, spacing=0.006, tail=0.04, lo=1300, hi=4000, seed=58)),
]
for sid, name, p in CLAPS:
    def _make(p=p, sid=sid):
        def f():
            x = clap_x(**p)
            if sid in ("x_clap_crunch", "x_clap_dutch"):
                x = drive(x * 1.5, 2.5)
            return polish(x, "clap")
        return f
    sound(sid, name, DRUMS, "Clap e schiocchi", colour=DRUM_COL["clap"])(_make())


@sound("x_snap_electro", "Schiocco Electro", DRUMS, "Clap e schiocchi", colour=DRUM_COL["clap"])
def _():
    d = 0.25; t = tt(d)
    x = bp(noise(d, 61), 1800, 6000) * np.exp(-t / 0.02) + np.sin(2 * np.pi * 1900 * t) * np.exp(-t / 0.006) * 0.4
    return polish(x, "clap")


@sound("x_clap_snare", "Clap + Snare Brostep", DRUMS, "Clap e schiocchi", colour=DRUM_COL["clap"])
def _():
    x = mix_at (clap_x(0.5, 4, 0.011, 0.12, 900, 2800, 62, room=0.12), snare_x(0.5, 190, 0.6, 0.06, 0.12, 1200, 8000, 0.8, 2.5, 63) * 0.6)
    return polish(x, "clap")


# ============================================================================ HI-HAT
def hat_x(d, tau, pitch, hpf, noise_amt, dist, seed, metal=0.6):
    t = tt(d)
    x = hat808(d, tau, pitch, hpf) * metal + hp(noise(d, seed), hpf) * np.exp(-t / (tau * 0.8)) * noise_amt
    x += click(d, seed + 3, 6000, 14000, 0.0008, 0.3)
    return drive(x * 1.5, dist) if dist > 1.0 else x


HATS = [
    ("x_hat_electro", "Hi-hat Electro",  dict(d=0.12, tau=0.030, pitch=1.0, hpf=7500, noise_amt=0.5, dist=1.0, seed=71)),
    ("x_hat_tick",    "Hi-hat Tick",     dict(d=0.08, tau=0.015, pitch=1.2, hpf=9000, noise_amt=0.4, dist=1.0, seed=72)),
    ("x_hat_crunch",  "Hi-hat Crunch",   dict(d=0.12, tau=0.030, pitch=0.9, hpf=6500, noise_amt=0.6, dist=3.0, seed=73)),
    ("x_hat_fidget",  "Hi-hat Fidget",   dict(d=0.10, tau=0.022, pitch=1.1, hpf=8000, noise_amt=0.8, dist=1.5, seed=74, metal=0.3)),
    ("x_hat_metal",   "Hi-hat Metallico", dict(d=0.14, tau=0.040, pitch=1.4, hpf=7000, noise_amt=0.2, dist=1.2, seed=75, metal=1.0)),
    ("x_hat_noise",   "Hi-hat Rumore",   dict(d=0.10, tau=0.025, pitch=1.0, hpf=8500, noise_amt=1.0, dist=1.0, seed=76, metal=0.1)),
    ("x_hat_soft",    "Hi-hat Morbido",  dict(d=0.12, tau=0.035, pitch=0.85, hpf=6000, noise_amt=0.5, dist=1.0, seed=77)),
    ("x_hat_dist",    "Hi-hat Distorto", dict(d=0.12, tau=0.028, pitch=1.0, hpf=7000, noise_amt=0.5, dist=5.0, seed=78)),
    ("x_hat_pedal",   "Hi-hat Pedale",   dict(d=0.10, tau=0.020, pitch=0.8, hpf=5500, noise_amt=0.4, dist=1.0, seed=79)),
    ("x_hat_shaker",  "Shaker Electro",  dict(d=0.14, tau=0.045, pitch=1.0, hpf=6000, noise_amt=1.0, dist=1.0, seed=80, metal=0.0)),
]
for sid, name, p in HATS:
    def _make(p=p, sid=sid):
        def f():
            x = hat_x(**p)
            if sid == "x_hat_shaker":
                x = x * np.clip(tt(p["d"]) / 0.012, 0, 1)   # attacco morbido
            return polish(x, "hat")
        return f
    sound(sid, name, DRUMS, "Hi-hat chiusi", colour=DRUM_COL["hat"])(_make())

OHATS = [
    ("x_ohat_electro", "Hi-hat aperto Electro",  dict(d=0.50, tau=0.18, pitch=1.0, hpf=7000, noise_amt=0.5, dist=1.0, seed=91)),
    ("x_ohat_long",    "Hi-hat aperto Lungo",    dict(d=0.80, tau=0.32, pitch=0.95, hpf=6500, noise_amt=0.5, dist=1.0, seed=92)),
    ("x_ohat_crunch",  "Hi-hat aperto Crunch",   dict(d=0.50, tau=0.20, pitch=0.9, hpf=6500, noise_amt=0.6, dist=3.0, seed=93)),
    ("x_ohat_fidget",  "Hi-hat aperto Fidget",   dict(d=0.35, tau=0.12, pitch=1.15, hpf=8000, noise_amt=0.8, dist=1.5, seed=94, metal=0.3)),
    ("x_ohat_metal",   "Hi-hat aperto Metallico", dict(d=0.60, tau=0.24, pitch=1.3, hpf=7000, noise_amt=0.2, dist=1.2, seed=95, metal=1.0)),
    ("x_ohat_noise",   "Hi-hat aperto Rumore",   dict(d=0.45, tau=0.16, pitch=1.0, hpf=8000, noise_amt=1.0, dist=1.0, seed=96, metal=0.1)),
    ("x_ohat_dist",    "Hi-hat aperto Distorto", dict(d=0.50, tau=0.20, pitch=1.0, hpf=7000, noise_amt=0.5, dist=4.0, seed=97)),
    ("x_ohat_short",   "Hi-hat aperto Corto",    dict(d=0.30, tau=0.10, pitch=1.0, hpf=7500, noise_amt=0.6, dist=1.0, seed=98)),
]
for sid, name, p in OHATS:
    def _make(p=p):
        return lambda: polish(hat_x(**p), "ohat")
    sound(sid, name, DRUMS, "Hi-hat aperti", colour=DRUM_COL["ohat"])(_make())


# ============================================================================ PIATTI
def crash_x(d, tau, lo, hi, n, nz, seed, dist=1.0):
    t = tt(d)
    x = metallic(d, lo, hi, n, tau, seed) * 0.8 + hp(noise(d, seed + 1), 4000) * np.exp(-t / (tau * 0.7)) * nz
    x *= np.clip(t / 0.002, 0, 1)
    return drive(x * 1.3, dist) if dist > 1.0 else x


@sound("x_crash_electro", "Crash Electro", DRUMS, "Piatti", colour=DRUM_COL["cym"])
def _():
    return polish(crash_x(2.0, 0.6, 3000, 14000, 40, 0.6, 101), "cym")


@sound("x_crash_dist", "Crash Distorto", DRUMS, "Piatti", colour=DRUM_COL["cym"])
def _():
    return polish(crash_x(1.8, 0.5, 2500, 13000, 40, 0.7, 102, dist=3.0), "cym")


@sound("x_ride_electro", "Ride Electro", DRUMS, "Piatti", colour=DRUM_COL["cym"])
def _():
    d = 1.2; t = tt(d)
    x = metallic(d, 2500, 9000, 24, 0.5, 103) * 0.6 + np.sin(2 * np.pi * 3400 * t) * np.exp(-t / 0.25) * 0.25
    return polish(x, "cym")


@sound("x_crash_reverse", "Crash al contrario", DRUMS, "Piatti", colour=DRUM_COL["cym"])
def _():
    x = crash_x(1.6, 0.5, 3000, 14000, 40, 0.6, 104)[::-1]
    return polish(fade(x, 0.05, 0.01), "cym")


@sound("x_china_electro", "China Electro", DRUMS, "Piatti", colour=DRUM_COL["cym"])
def _():
    return polish(crash_x(1.4, 0.4, 1800, 9000, 50, 0.8, 105, dist=2.0), "cym")


@sound("x_splash_electro", "Splash Electro", DRUMS, "Piatti", colour=DRUM_COL["cym"])
def _():
    return polish(crash_x(0.8, 0.22, 3500, 15000, 30, 0.6, 106), "cym")


# ============================================================================ TOM
def tom_x(d, f0, f1, tau, seed, dist=1.5):
    t = tt(d)
    f = pitch_env(d, f0, f1, 0.04)
    x = np.sin(phase_of(f)) * np.exp(-t / tau) + click(d, seed, 1500, 6000, 0.002, 0.4)
    x += bp(noise(d, seed + 1), 300, 2000) * np.exp(-t / 0.03) * 0.15
    return drive(x, dist)


TOMS = [
    ("x_tom_high",  "Tom Electro alto",  dict(d=0.45, f0=320, f1=200, tau=0.16, seed=111)),
    ("x_tom_mid",   "Tom Electro medio", dict(d=0.55, f0=240, f1=145, tau=0.20, seed=112)),
    ("x_tom_low",   "Tom Electro basso", dict(d=0.65, f0=180, f1=100, tau=0.24, seed=113)),
    ("x_tom_floor", "Tom Electro profondo", dict(d=0.75, f0=140, f1=72, tau=0.28, seed=114)),
    ("x_tom_dist",  "Tom Distorto",      dict(d=0.55, f0=260, f1=120, tau=0.20, seed=115, dist=4.0)),
    ("x_tom_brostep", "Tom Brostep",     dict(d=0.70, f0=300, f1=90, tau=0.26, seed=116, dist=2.5)),
]
for sid, name, p in TOMS:
    def _make(p=p):
        return lambda: polish(tom_x(**p), "tom")
    sound(sid, name, DRUMS, "Tom", colour=DRUM_COL["tom"])(_make())


# ============================================================================ PERCUSSIONI
@sound("x_perc_rim", "Rim Electro", DRUMS, "Percussioni", colour=DRUM_COL["perc"])
def _():
    d = 0.15; t = tt(d)
    x = np.sin(2 * np.pi * 1700 * t) * np.exp(-t / 0.012) + bp(noise(d, 121), 2000, 8000) * np.exp(-t / 0.006) * 0.6
    return polish(x, "perc")


@sound("x_perc_cowbell", "Cowbell Electro", DRUMS, "Percussioni", colour=DRUM_COL["perc"])
def _():
    d = 0.4; t = tt(d)
    x = (np.sign(np.sin(2 * np.pi * 562 * t)) + np.sign(np.sin(2 * np.pi * 845 * t))) * 0.5
    x = bp(x, 500, 3500) * np.exp(-t / 0.09)
    return polish(drive(x, 1.6), "perc")


@sound("x_perc_zap", "Perc Zap", DRUMS, "Percussioni", colour=DRUM_COL["perc"])
def _():
    d = 0.18; t = tt(d)
    f = pitch_env(d, 3000, 200, 0.02)
    return polish(np.sin(phase_of(f)) * np.exp(-t / 0.05), "perc")


@sound("x_perc_metal", "Perc Metallica", DRUMS, "Percussioni", colour=DRUM_COL["perc"])
def _():
    d = 0.35
    return polish(metallic(d, 900, 5000, 12, 0.08, 124) * 1.5, "perc")


@sound("x_perc_blip", "Blip", DRUMS, "Percussioni", colour=DRUM_COL["perc"])
def _():
    d = 0.12; t = tt(d)
    return polish(np.sin(2 * np.pi * 1320 * t) * np.exp(-t / 0.025) + np.sin(2 * np.pi * 2640 * t) * np.exp(-t / 0.01) * 0.3, "perc")


@sound("x_perc_wood", "Legno Electro", DRUMS, "Percussioni", colour=DRUM_COL["perc"])
def _():
    d = 0.15; t = tt(d)
    x = np.sin(2 * np.pi * 880 * t) * np.exp(-t / 0.02) + np.sin(2 * np.pi * 1750 * t) * np.exp(-t / 0.008) * 0.4
    return polish(x, "perc")


@sound("x_perc_laser", "Perc Laser", DRUMS, "Percussioni", colour=DRUM_COL["perc"])
def _():
    d = 0.25; t = tt(d)
    f = pitch_env(d, 5000, 600, 0.04)
    return polish(drive(np.sin(phase_of(f)) * np.exp(-t / 0.08), 2.0), "perc")


@sound("x_perc_noisehit", "Colpo di rumore", DRUMS, "Percussioni", colour=DRUM_COL["perc"])
def _():
    d = 0.2; t = tt(d)
    return polish(bp(noise(d, 128), 600, 6000) * np.exp(-t / 0.035), "perc")


# ============================================================================ BASSI (C2 = 36, tenuti)
BASS_COL = 0xff9a72e0
SYNTH_COL = 0xff4fb3e8
FX_COL = 0xffe36fae
VOX_COL = 0xffd99a5b


def detuned_saw(f, d, voices=5, spread=0.012, seed=0):
    out = np.zeros(secs(d))
    for v in range(voices):
        det = 1 + spread * (v - (voices - 1) / 2) / max(1, (voices - 1) / 2)
        out += saw(f * det, d, seed=seed + v)
    return out / np.sqrt(voices)


def bass_note(fn, d=1.6, rel=0.08):
    """Basso tenuto: attacco veloce, sustain pieno, coda corta (il piano roll decide la durata)."""
    x = fn(d)
    return x * adsr(d, 0.003, 0.3, 0.85)


@sound("x_bass_growl", "Basso Complextro Growl", INSTR, "Bassi", root=36, gated=True, release=0.06, colour=BASS_COL)
def _():
    d = 1.6; f = mtof(36); t = tt(d)
    idx = 2.5 + 2.0 * np.sin(2 * np.pi * 3.0 * t)      # "parla": l'indice FM si muove
    x = np.sin(2 * np.pi * f * t + idx * np.sin(2 * np.pi * f * 2.0 * t))
    x = drive(x, 3.0)
    vow = formants(x, [600, 1100, 2400], [200, 260, 400], [1.0, 0.7, 0.3]) * 1.2 + lp(x, 300) * 0.8
    return polish(bass_note(lambda d: vow, d), "bass")


@sound("x_bass_saw", "Basso Electro Saw", INSTR, "Bassi", root=36, gated=True, release=0.05, colour=BASS_COL)
def _():
    d = 1.6; f = mtof(36)
    x = detuned_saw(f, d, 3, 0.008, 201)
    cut = 300 + 2500 * np.exp(-tt(d) / 0.15)
    x = svf(x, cut, 1.2) + np.sin(phase_of(np.full(secs(d), f))) * 0.5
    return polish(bass_note(lambda d: drive(x, 2.0), d), "bass")


@sound("x_bass_wub", "Basso Fidget Wub", INSTR, "Bassi", root=36, gated=True, release=0.05, colour=BASS_COL)
def _():
    d = 1.6; f = mtof(36); t = tt(d)
    x = saw(f, d, seed=202) * 0.7 + square(f * 0.5, d, seed=203) * 0.5
    lfo = 0.5 - 0.5 * np.cos(2 * np.pi * 4.0 * t)      # wub a 1/8 a 120 BPM
    x = svf(x, 150 + 2200 * lfo, 2.0)
    return polish(bass_note(lambda d: drive(x, 2.2), d), "bass")


@sound("x_bass_square", "Basso Square Sporco", INSTR, "Bassi", root=36, gated=True, release=0.05, colour=BASS_COL)
def _():
    d = 1.6; f = mtof(36)
    x = square(f, d, seed=204) + 0.6 * np.sin(phase_of(np.full(secs(d), f / 2)))
    x = lp(drive(x, 3.0), 1800)
    return polish(bass_note(lambda d: x, d), "bass")


@sound("x_bass_yoi", "Basso Brostep Yoi", INSTR, "Bassi", root=36, gated=True, release=0.06, colour=BASS_COL)
def _():
    d = 1.6; f = mtof(36); t = tt(d)
    x = drive(detuned_saw(f, d, 3, 0.01, 205) + square(f, d, seed=208) * 0.5, 3.0)
    # vocale che si apre: "o" -> "i" (lo "yoi" del brostep)
    a = np.clip(t / 0.35, 0, 1)
    o = formants(x, *VOWELS["o"]); i_ = formants(x, *VOWELS["i"])
    y = o * (1 - a) + i_ * a + lp(x, 180) * 0.7
    return polish(bass_note(lambda d: y * 1.5, d), "bass")


@sound("x_bass_reese", "Basso Reese Electro", INSTR, "Bassi", root=36, gated=True, release=0.08, colour=BASS_COL)
def _():
    d = 1.6; f = mtof(36)
    x = saw(f * 0.995, d, seed=206) + saw(f * 1.006, d, seed=207)
    x = lp(drive(x, 2.0), 1200) + np.sin(phase_of(np.full(secs(d), f))) * 0.4
    return polish(bass_note(lambda d: x, d), "bass")


@sound("x_bass_hoover", "Basso Hoover", INSTR, "Bassi", root=36, gated=True, release=0.1, colour=BASS_COL)
def _():
    d = 1.6; f0 = mtof(36); t = tt(d)
    f = f0 * (1 - 0.25 * np.exp(-t / 0.08))            # scivola su dalla nota sotto
    x = np.zeros(len(t))
    for k, det in enumerate([0.985, 0.995, 1.0, 1.006, 1.017]):
        x += np.sign(np.sin(phase_of(f * det) + k)) * 0.5 + (2 * ((phase_of(f * det) / (2 * np.pi)) % 1) - 1) * 0.5
    x = lp(x / 3.0, 2500)
    x = chorus(x, 8, 0.8, 0.4, seed=209)
    return polish(bass_note(lambda d: drive(x, 1.5), d), "bass")


@sound("x_bass_donk", "Basso Donk (fidget)", INSTR, "Bassi", root=36, colour=BASS_COL)
def _():
    d = 0.35; f = mtof(36); t = tt(d)
    x = np.sin(phase_of(f * (1 + 2.0 * np.exp(-t / 0.01)))) * np.exp(-t / 0.12)
    x += np.sin(2 * np.pi * f * 3.02 * t) * np.exp(-t / 0.03) * 0.5
    return polish(drive(x, 1.8), "bass")


@sound("x_bass_sub", "Sub Electro", INSTR, "Bassi", root=36, gated=True, release=0.08, colour=BASS_COL)
def _():
    d = 1.6; f = mtof(36)
    x = np.sin(phase_of(np.full(secs(d), f))) + 0.15 * np.sin(phase_of(np.full(secs(d), f * 2)))
    return polish(bass_note(lambda d: drive(x, 1.3), d), "bass")


@sound("x_bass_laser", "Basso Laser", INSTR, "Bassi", root=36, gated=True, release=0.05, colour=BASS_COL)
def _():
    d = 1.6; f = mtof(36); t = tt(d)
    idx = 6.0 * np.exp(-t / 0.08) + 1.0
    x = np.sin(2 * np.pi * f * t + idx * np.sin(2 * np.pi * f * 3.0 * t))
    return polish(bass_note(lambda d: drive(x, 2.5), d), "bass")


# ============================================================================ SYNTH E STAB (C4 = 60)
def chord(fn, root, intervals, d):
    return sum(fn(mtof(root + i), d) for i in intervals) / np.sqrt(len(intervals))


@sound("x_stab_dutch", "Stab Dirty Dutch", INSTR, "Synth e stab", root=60, colour=SYNTH_COL)
def _():
    d = 0.6; t = tt(d)
    x = detuned_saw(mtof(60), d, 7, 0.02, 301)
    x = svf(x, 800 + 7000 * np.exp(-t / 0.06), 1.0) * np.exp(-t / 0.18)
    return polish(drive(x, 2.0), "synth")


@sound("x_stab_saw", "Stab Electro Saw", INSTR, "Synth e stab", root=60, colour=SYNTH_COL)
def _():
    d = 0.5; t = tt(d)
    x = detuned_saw(mtof(60), d, 5, 0.012, 302)
    x = lp(x, 5000) * np.exp(-t / 0.12)
    return polish(x, "synth")


@sound("x_lead_screech", "Lead Screech", INSTR, "Synth e stab", root=60, gated=True, release=0.08, colour=SYNTH_COL)
def _():
    d = 1.4; f = mtof(60); t = tt(d)
    idx = 4.0 + 2.5 * np.sin(2 * np.pi * 6.0 * t)
    x = np.sin(2 * np.pi * f * t + idx * np.sin(2 * np.pi * f * 1.5 * t))
    x = drive(x, 3.5)
    return polish(x * adsr(d, 0.005, 0.2, 0.9), "synth")


@sound("x_lead_square", "Lead Square", INSTR, "Synth e stab", root=60, gated=True, release=0.06, colour=SYNTH_COL)
def _():
    d = 1.4; f = mtof(60)
    x = square(f, d, seed=304) * 0.7 + square(f * 1.004, d, seed=305) * 0.5
    return polish(lp(x, 6000) * adsr(d, 0.004, 0.2, 0.8), "synth")


@sound("x_stab_fidget", "Stab Fidget (accordo)", INSTR, "Synth e stab", root=60, colour=SYNTH_COL)
def _():
    d = 0.5; t = tt(d)
    x = chord(lambda f, d: saw(f, d, seed=306) + square(f, d, seed=307) * 0.5, 60, [0, 3, 7, 10], d)
    x = svf(x, 600 + 5000 * np.exp(-t / 0.05), 1.4) * np.exp(-t / 0.12)
    return polish(drive(x, 1.8), "synth")


@sound("x_pluck_electro", "Pluck Electro", INSTR, "Synth e stab", root=60, colour=SYNTH_COL)
def _():
    d = 0.5; t = tt(d)
    x = detuned_saw(mtof(60), d, 3, 0.008, 308)
    x = svf(x, 300 + 6000 * np.exp(-t / 0.03), 0.9) * np.exp(-t / 0.15)
    return polish(x, "synth")


@sound("x_stab_organ", "Stab Organo House", INSTR, "Synth e stab", root=60, colour=SYNTH_COL)
def _():
    d = 0.5; t = tt(d); f = mtof(60)
    x = sum(np.sin(2 * np.pi * f * h * t) * a for h, a in [(0.5, 0.5), (1, 1.0), (2, 0.6), (3, 0.4), (4, 0.25), (6, 0.15)])
    x = chord(lambda ff, d: sum(np.sin(2 * np.pi * ff * h * tt(d)) * a for h, a in [(1, 1.0), (2, 0.6), (3, 0.35)]), 60, [0, 3, 7], d)
    return polish(drive(x * np.exp(-t / 0.2), 1.6), "synth")


@sound("x_lead_hoover", "Lead Hoover", INSTR, "Synth e stab", root=60, gated=True, release=0.1, colour=SYNTH_COL)
def _():
    d = 1.4; f0 = mtof(60); t = tt(d)
    f = f0 * (1 - 0.2 * np.exp(-t / 0.06))
    x = np.zeros(len(t))
    for k, det in enumerate([0.98, 0.993, 1.0, 1.008, 1.02]):
        x += (2 * ((phase_of(f * det) / (2 * np.pi)) % 1) - 1)
    x = chorus(lp(x / 3, 4000), 9, 0.7, 0.5, seed=309)
    return polish(x * adsr(d, 0.01, 0.3, 0.85), "synth")


@sound("x_stab_brass", "Stab Brass Electro", INSTR, "Synth e stab", root=60, colour=SYNTH_COL)
def _():
    d = 0.6; t = tt(d)
    x = chord(lambda f, d: saw(f, d, seed=310), 60, [0, 7, 12], d)
    x = svf(x, 400 + 3000 * (1 - np.exp(-t / 0.03)) * np.exp(-t / 0.3), 1.0) * adsr(d, 0.01, 0.2, 0.0)
    return polish(drive(x, 1.5), "synth")


@sound("x_arp_pluck", "Pluck Arpeggio", INSTR, "Synth e stab", root=60, colour=SYNTH_COL)
def _():
    d = 0.3; t = tt(d)
    x = square(mtof(60), d, seed=311) * 0.6 + saw(mtof(72), d, seed=312) * 0.3
    x = svf(x, 400 + 5000 * np.exp(-t / 0.02), 1.2) * np.exp(-t / 0.08)
    return polish(x, "synth")


# ============================================================================ FX
@sound("x_fx_riser", "Riser (8 battute)", INSTR, "FX", colour=FX_COL)
def _():
    d = 4.0; t = tt(d)
    a = t / d
    x = svf(noise(d, 401), 400 + 12000 * a ** 2, 1.5, mode="bp") * (0.2 + 0.8 * a)
    return polish(fade(x, 0.0, 0.02), "fx")


@sound("x_fx_riser_tone", "Riser con nota", INSTR, "FX", colour=FX_COL)
def _():
    d = 4.0; t = tt(d)
    f = 100 * 2 ** (4 * t / d)
    x = (2 * ((phase_of(f) / (2 * np.pi)) % 1) - 1) * (0.2 + 0.8 * t / d)
    return polish(lp(x, 8000), "fx")


@sound("x_fx_downlifter", "Downlifter", INSTR, "FX", colour=FX_COL)
def _():
    d = 2.0; t = tt(d)
    a = 1 - t / d
    x = svf(noise(d, 402), 300 + 9000 * a ** 2, 1.4, mode="bp") * a
    return polish(x, "fx")


@sound("x_fx_impact", "Impatto", INSTR, "FX", colour=FX_COL)
def _():
    d = 2.0; t = tt(d)
    f = pitch_env(d, 120, 35, 0.15)
    x = np.sin(phase_of(f)) * np.exp(-t / 0.6) + lp(noise(d, 403), 2000) * np.exp(-t / 0.15) * 0.6
    return polish(reverb(drive(x, 2.0), 0.25, 1.5, 0.5, 4000), "fx")


@sound("x_fx_zap", "Zap", INSTR, "FX", colour=FX_COL)
def _():
    d = 0.4; t = tt(d)
    f = pitch_env(d, 4000, 80, 0.05)
    return polish(drive(np.sin(phase_of(f)) * np.exp(-t / 0.15), 2.5), "fx")


@sound("x_fx_laser", "Laser", INSTR, "FX", colour=FX_COL)
def _():
    d = 0.6; t = tt(d)
    f = 2000 * 2 ** (-3 * t / d) * (1 + 0.05 * np.sin(2 * np.pi * 30 * t))
    return polish(np.sin(phase_of(f)) * np.exp(-t / 0.25), "fx")


@sound("x_fx_sweep", "Sweep di rumore", INSTR, "FX", colour=FX_COL)
def _():
    d = 1.5; t = tt(d)
    x = svf(noise(d, 404), 200 * 2 ** (6 * t / d), 2.0, mode="bp") * np.sin(np.pi * t / d)
    return polish(x, "fx")


@sound("x_fx_reverse_snare", "Rullante al contrario", INSTR, "FX", colour=FX_COL)
def _():
    x = snare_x(0.9, 200, 0.5, 0.07, 0.25, 1200, 9000, 0.6, 2.0, 405, room=0.4)[::-1]
    return polish(fade(x, 0.05, 0.005), "fx")


@sound("x_fx_siren", "Sirena", INSTR, "FX", colour=FX_COL)
def _():
    d = 2.0; t = tt(d)
    f = 900 + 500 * np.sin(2 * np.pi * 1.5 * t)
    x = (2 * ((phase_of(f) / (2 * np.pi)) % 1) - 1)
    return polish(lp(x, 5000) * adsr(d, 0.05, 0.5, 0.9, rel_start=1.7, rel=0.1), "fx")


@sound("x_fx_glitch", "Glitch", INSTR, "FX", colour=FX_COL)
def _():
    d = 0.6; t = tt(d)
    base = detuned_saw(mtof(60), 0.05, 3, 0.01, 406)
    out = np.zeros(secs(d))
    r = rng(407)
    pos = 0
    while pos < len(out) - len(base):
        g = r.uniform(0.3, 1.0)
        out[pos:pos + len(base)] += base * g * np.hanning(len(base))
        pos += secs(r.choice([0.03125, 0.0625]))
    return polish(drive(out, 2.0), "fx")


# ============================================================================ VOCI (formanti, stile chop)
def vox(vowels, d, f0, seed, glide=0.25):
    t = tt(d)
    f = f0 * (1 + 0.01 * np.sin(2 * np.pi * 5.5 * t))
    src = (2 * ((phase_of(f) / (2 * np.pi)) % 1) - 1) + hp(noise(d, seed), 3000) * 0.05
    if len(vowels) == 1:
        y = formants(src, *VOWELS[vowels[0]])
    else:
        a = np.clip(t / glide, 0, 1)
        y = formants(src, *VOWELS[vowels[0]]) * (1 - a) + formants(src, *VOWELS[vowels[1]]) * a
    return y * adsr(d, 0.01, 0.3, 0.8, rel_start=d - 0.08, rel=0.03)


@sound("x_vox_hey", "Vox Hey", INSTR, "Voci", root=60, colour=VOX_COL)
def _():
    return polish(vox(["e", "i"], 0.35, mtof(60), 501, 0.15), "vox")


@sound("x_vox_oh", "Vox Oh", INSTR, "Voci", root=60, colour=VOX_COL)
def _():
    return polish(vox(["o"], 0.45, mtof(60), 502), "vox")


@sound("x_vox_yeah", "Vox Yeah", INSTR, "Voci", root=60, colour=VOX_COL)
def _():
    return polish(vox(["i", "a"], 0.5, mtof(60), 503, 0.2), "vox")


@sound("x_vox_uh", "Vox Uh", INSTR, "Voci", root=60, colour=VOX_COL)
def _():
    return polish(vox(["u"], 0.3, mtof(60), 504), "vox")


@sound("x_vox_chop", "Vox Fidget Chop", INSTR, "Voci", root=60, colour=VOX_COL)
def _():
    x = vox(["a", "o"], 0.25, mtof(60), 505, 0.1)
    return polish(drive(x * 2.0, 2.0), "vox")


@sound("x_vox_aah", "Vox Aah", INSTR, "Voci", root=60, gated=True, release=0.15, colour=VOX_COL)
def _():
    return polish(chorus(vox(["a"], 1.5, mtof(60), 506), 7, 0.6, 0.4, seed=507), "vox")
