"""
Piccolo kit DSP per sintetizzare la libreria suoni di JLoop (offline, con numpy/scipy).
Tutti i suoni sono generati da zero: nessun campione esterno, nessun problema di diritti.
"""
import numpy as np
import scipy.signal as sg

SR = 44100


def secs(d):
    return int(round(d * SR))


def tt(d):
    return np.arange(secs(d)) / SR


def mtof(m):
    return 440.0 * 2.0 ** ((m - 69) / 12.0)


def rng(seed):
    return np.random.default_rng(seed)


def noise(d, seed=1):
    return rng(seed).uniform(-1.0, 1.0, secs(d))


def expenv(d, tau):
    return np.exp(-tt(d) / max(tau, 1e-5))


def adsr(d, a, dcy, s, rel_start=None, rel=0.2):
    """Inviluppo ADSR; rel_start = istante (s) in cui parte il rilascio (None = mai)."""
    t = tt(d)
    env = np.ones_like(t)
    if a > 0:
        env = np.where(t < a, t / a, env)
    dec = s + (1 - s) * np.exp(-(t - a) / max(dcy, 1e-4))
    env = np.where(t >= a, dec, env)
    if rel_start is not None:
        lvl = np.interp(rel_start, t, env)
        env = np.where(t >= rel_start, lvl * np.exp(-(t - rel_start) / max(rel, 1e-4)), env)
    return env


def fade(x, fin=0.0, fout=0.01):
    x = x.copy()
    if fin > 0:
        n = min(len(x), secs(fin))
        x[:n] *= np.linspace(0, 1, n)
    n = min(len(x), secs(fout))
    if n > 1:
        x[-n:] *= np.linspace(1, 0, n) ** 2
    return x


def phase_of(freq):
    """Fase (radianti) di un oscillatore a frequenza variabile (array) o costante."""
    return 2 * np.pi * np.cumsum(freq) / SR


def as_array(f, n):
    return np.full(n, float(f)) if np.isscalar(f) else np.asarray(f, dtype=float)


def additive(freq, d, amp_of_k, kmax=400, phase0=None, seed=0):
    """Somma di armoniche k*freq con ampiezza amp_of_k(k); niente aliasing (si ferma sotto Nyquist)."""
    n = secs(d)
    f = as_array(freq, n)
    ph = phase_of(f)
    out = np.zeros(n)
    r = rng(seed)
    fmax = f.max() if len(f) else 1.0
    for k in range(1, kmax + 1):
        a = amp_of_k(k)
        if a == 0:
            continue
        if k * f.min() > SR * 0.45:
            break
        mask = np.clip((SR * 0.45 - k * f) / (SR * 0.05), 0, 1)
        p0 = 0.0 if phase0 is None else r.uniform(0, 2 * np.pi)
        out += a * mask * np.sin(k * ph + p0)
    return out


def saw(freq, d, seed=0, rand_phase=True):
    return additive(freq, d, lambda k: (-1) ** (k + 1) / k * 0.63, phase0=(1 if rand_phase else None), seed=seed)


def square(freq, d, seed=0, rand_phase=True):
    return additive(freq, d, lambda k: (1.0 / k if k % 2 else 0.0) * 0.8, phase0=(1 if rand_phase else None), seed=seed)


def triangle(freq, d):
    return additive(freq, d, lambda k: ((-1) ** ((k - 1) // 2) / (k * k) if k % 2 else 0.0) * 0.8)


def pulse(freq, d, width=0.25, seed=0):
    return additive(freq, d, lambda k: np.sin(np.pi * k * width) / k * 0.8, phase0=1, seed=seed)


def sine(freq, d, phase0=0.0):
    n = secs(d)
    return np.sin(phase_of(as_array(freq, n)) + phase0)


def cents(c):
    return 2.0 ** (c / 1200.0)


def vibrato(d, base, rate=5.5, depth_cents=12, delay=0.25, seed=0):
    t = tt(d)
    ramp = np.clip((t - delay) / 0.4, 0, 1)
    r = rng(seed)
    wob = np.sin(2 * np.pi * rate * t + r.uniform(0, 6.28)) + 0.15 * np.sin(2 * np.pi * rate * 0.37 * t)
    return base * cents(depth_cents * ramp * wob)


# ---------------------------------------------------------------- filtri

def _sos(kind, fc, order=2, fc2=None):
    nyq = SR / 2
    if kind == "band":
        return sg.butter(order, [max(10, fc) / nyq, min(fc2, nyq * 0.95) / nyq], btype="band", output="sos")
    return sg.butter(order, min(max(10, fc), nyq * 0.95) / nyq, btype=kind, output="sos")


def lp(x, fc, order=2):
    return sg.sosfilt(_sos("low", fc, order), x)


def hp(x, fc, order=2):
    return sg.sosfilt(_sos("high", fc, order), x)


def bp(x, lo, hi, order=2):
    return sg.sosfilt(_sos("band", lo, order, hi), x)


def bp_q(x, fc, q):
    bw = fc / q
    return bp(x, max(20, fc - bw / 2), fc + bw / 2)


def peak(x, fc, q, gain_db):
    """Equalizzatore a campana (RBJ)."""
    A = 10 ** (gain_db / 40)
    w = 2 * np.pi * fc / SR
    al = np.sin(w) / (2 * q)
    b = [1 + al * A, -2 * np.cos(w), 1 - al * A]
    a = [1 + al / A, -2 * np.cos(w), 1 - al / A]
    return sg.lfilter(b, a, x)


def svf(x, cutoff, res=0.7, mode="lp"):
    """Filtro State-Variable con cutoff variabile campione per campione (array o costante)."""
    n = len(x)
    fc = as_array(cutoff, n)
    g = np.tan(np.pi * np.clip(fc, 20, SR * 0.45) / SR)
    k = 1.0 / max(res, 0.05)
    a1 = 1.0 / (1.0 + g * (g + k))
    a2 = g * a1
    a3 = g * a2
    ic1 = ic2 = 0.0
    out = np.empty(n)
    xs = x.tolist(); A1 = a1.tolist(); A2 = a2.tolist(); A3 = a3.tolist()
    want = {"lp": 0, "bp": 1, "hp": 2}[mode]
    for i in range(n):
        v3 = xs[i] - ic2
        v1 = A1[i] * ic1 + A2[i] * v3
        v2 = ic2 + A2[i] * ic1 + A3[i] * v3
        ic1 = 2 * v1 - ic1
        ic2 = 2 * v2 - ic2
        if want == 0:
            out[i] = v2
        elif want == 1:
            out[i] = v1
        else:
            out[i] = xs[i] - k * v1 - v2
    return out


def ladder(x, cutoff, res=0.0, drive=1.0):
    """Passa-basso 4 poli stile "ladder" con risonanza (per acid e bassi)."""
    n = len(x)
    fc = as_array(cutoff, n)
    g = 1 - np.exp(-2 * np.pi * np.clip(fc, 20, SR * 0.42) / SR)
    s = [0.0, 0.0, 0.0, 0.0]
    out = np.empty(n)
    xs = (x * drive).tolist(); G = g.tolist()
    y = 0.0
    for i in range(n):
        u = _tanh(xs[i] - 4.0 * res * y)
        gi = G[i]
        s[0] += gi * (u - s[0])
        s[1] += gi * (s[0] - s[1])
        s[2] += gi * (s[1] - s[2])
        s[3] += gi * (s[2] - s[3])
        y = s[3]
        out[i] = y
    return out


def _tanh(v):
    # tanh veloce per i loop in Python
    if v > 3:
        return 1.0
    if v < -3:
        return -1.0
    v2 = v * v
    return v * (27 + v2) / (27 + 9 * v2)


def drive(x, amount, makeup=True):
    y = np.tanh(x * amount)
    return y / np.tanh(amount) if makeup else y


def formants(x, freqs, bws, gains):
    out = np.zeros_like(x)
    for f, b, g in zip(freqs, bws, gains):
        out += g * bp(x, max(30, f - b / 2), f + b / 2)
    return out


VOWELS = {
    "a": ([730, 1090, 2440, 3400], [90, 110, 170, 250], [1.0, 0.5, 0.25, 0.12]),
    "o": ([570, 840, 2410, 3300], [80, 100, 170, 250], [1.0, 0.45, 0.15, 0.08]),
    "u": ([300, 870, 2240, 3200], [60, 100, 170, 250], [1.0, 0.25, 0.08, 0.05]),
    "e": ([530, 1840, 2480, 3500], [70, 120, 170, 250], [1.0, 0.45, 0.25, 0.1]),
    "i": ([270, 2290, 3010, 3700], [60, 130, 180, 250], [1.0, 0.35, 0.25, 0.1]),
}


# ---------------------------------------------------------------- generatori fisici

def karplus(freq, d, decay=0.996, bright=0.5, pluck_pos=0.18, seed=3, excitation=None, tilt=0.6):
    """Corda pizzicata (Karplus-Strong esteso): eccitazione filtrata, perdita in frequenza regolabile."""
    n = secs(d)
    L = SR / freq
    N = int(np.floor(L))
    frac = L - N
    r = rng(seed)
    if excitation is None:
        # forma della corda tirata dal plettro: un triangolo col vertice nel punto di pizzico
        # (armoniche ~ sin(n*pi*p)/n^2: fondamentale piena, come una corda vera)
        pos = np.arange(N) / N
        p = min(max(pluck_pos, 0.03), 0.97)
        shape = np.where(pos < p, pos / p, (1 - pos) / (1 - p))
        # un po' di rumore filtrato per l'attacco (piu' "bright" = attacco piu' brillante)
        nz = sg.lfilter([0.5 + 0.5 * bright], [1, -(0.5 - 0.5 * bright)], r.uniform(-1, 1, N))
        exc = shape * 2 + nz * 0.35 * bright
        # piu' scuro = spigolo del triangolo arrotondato
        if bright < 0.9:
            k = max(1, int(N * 0.08 * (1 - bright)))
            exc = np.convolve(np.concatenate([exc, exc[:k]]), np.ones(k) / k, mode="same")[:N]
        exc -= exc.mean()
    else:
        exc = excitation[:N]
    buf = np.zeros(N + 2)
    buf[:N] = exc
    out = np.empty(n)
    b = buf.tolist()
    size = N + 2
    w = 0
    prev = 0.0
    # loss filter: media pesata (piu' "bright" = meno perdita sugli acuti)
    s = 0.5 * (1 - 0.4 * bright)
    ap_c = (1 - frac) / (1 + frac)     # allpass per l'accordatura fine
    ap_x1 = ap_y1 = 0.0
    for i in range(n):
        rd = (w - N) % size
        x0 = b[rd]
        x1 = b[(rd + 1) % size]
        y = decay * ((1 - s) * x0 + s * x1)
        ap = ap_c * y + ap_x1 - ap_c * ap_y1
        ap_x1, ap_y1 = y, ap
        b[w] = ap
        out[i] = x0
        w = (w + 1) % size
    # Il suono esce dal ponte: conta anche la "pendenza" della corda (spettro ~1/n invece di 1/n^2)
    deriv = np.diff(out, prepend=0.0) * SR / (2 * np.pi * freq)
    return (1 - tilt) * out + tilt * deriv


def modal(freq, d, ratios, amps, taus, seed=0, detune_cents=0.0):
    """Somma di modi sinusoidali che decadono (barre, campane, membrane)."""
    t = tt(d)
    out = np.zeros_like(t)
    r = rng(seed)
    for ra, a, ta in zip(ratios, amps, taus):
        f = freq * ra * cents(r.uniform(-detune_cents, detune_cents))
        if f > SR * 0.45:
            continue
        out += a * np.sin(2 * np.pi * f * t + r.uniform(0, 6.28)) * np.exp(-t / ta)
    return out


def metallic(d, f_lo, f_hi, n_partials, tau, seed=7, tau_spread=0.5):
    """Piatti e campanelli: tante parziali inarmoniche con decadimenti diversi."""
    t = tt(d)
    r = rng(seed)
    out = np.zeros_like(t)
    for _ in range(n_partials):
        f = np.exp(r.uniform(np.log(f_lo), np.log(f_hi)))
        ta = tau * np.exp(r.uniform(-tau_spread, tau_spread)) * (1.2 - 0.4 * np.log(f / f_lo) / np.log(f_hi / f_lo))
        out += np.sin(2 * np.pi * f * t + r.uniform(0, 6.28)) * np.exp(-t / ta) * r.uniform(0.4, 1.0)
    return out / np.sqrt(n_partials)


def hat808(d, tau, pitch=1.0, hpf=7000):
    t = tt(d)
    freqs = np.array([205.3, 304.4, 369.6, 522.7, 540.0, 800.0]) * pitch
    sq = sum(np.sign(np.sin(2 * np.pi * f * t + i)) for i, f in enumerate(freqs)) / 6
    x = bp(sq, hpf, min(16000, hpf * 2.2), order=2)
    return x * np.exp(-t / tau)


def fm(freq, d, ratio, index_env, carrier_ratio=1.0, fb=0.0):
    t = tt(d)
    mod = np.sin(2 * np.pi * freq * ratio * t)
    return np.sin(2 * np.pi * freq * carrier_ratio * t + index_env * mod)


# ---------------------------------------------------------------- spazio

def reverb_ir(length, tau, bright=6000, seed=11, predelay=0.012):
    n = secs(length)
    r = rng(seed)
    t = np.arange(n) / SR
    ir = r.normal(0, 1, n) * np.exp(-t / tau)
    dark = lp(ir, bright * 0.25)
    mixw = np.clip(t / (tau * 2), 0, 1)
    ir = ir * (1 - mixw) + dark * mixw * 2.0
    ir = lp(ir, bright)
    pd = secs(predelay)
    ir = np.concatenate([np.zeros(pd), ir])
    # prime riflessioni
    for k, (dt, g) in enumerate([(0.007, 0.5), (0.013, 0.35), (0.021, 0.3), (0.029, 0.2)]):
        i = secs(dt)
        if i < len(ir):
            ir[i] += g * (1 if k % 2 == 0 else -1) * 3
    return ir / np.sqrt(np.sum(ir ** 2))


# Percussioni: riverbero molto ridotto (le code si sommavano colpo dopo colpo e "sporcavano" il mix).
# Lo imposta generate.py prima di creare ogni suono di batteria / percussione.
PERCUSSION = {"on": False}


def reverb(x, mix=0.2, length=1.5, tau=0.35, bright=6000, seed=11, tail=True):
    if PERCUSSION["on"]:
        mix *= 0.3
        tau *= 0.45
        length = max(0.3, length * 0.5)
    ir = reverb_ir(length, tau, bright, seed)
    wet = sg.fftconvolve(x, ir)
    if not tail:
        wet = wet[:len(x)]
    dry = np.concatenate([x, np.zeros(len(wet) - len(x))])
    wet *= np.sqrt(np.sum(dry ** 2) / max(np.sum(wet ** 2), 1e-12))
    return dry * (1 - mix * 0.5) + wet * mix


def echo(x, delay, feedback=0.4, mix=0.35, damp=3000, repeats=6):
    n = len(x) + secs(delay) * repeats
    out = np.zeros(n)
    out[:len(x)] += x
    tap = x.copy()
    g = 1.0
    for k in range(1, repeats + 1):
        g *= feedback
        tap = lp(tap, damp)
        i = secs(delay) * k
        out[i:i + len(tap)] += tap * g * mix
    return out


def chorus(x, depth_ms=6, rate=0.6, mix=0.5, seed=0):
    n = len(x)
    t = np.arange(n) / SR
    base = secs(0.012)
    r = rng(seed)
    d = base + depth_ms / 1000 * SR * 0.5 * (1 + np.sin(2 * np.pi * rate * t + r.uniform(0, 6)))
    idx = np.arange(n) - d
    idx = np.clip(idx, 0, n - 1)
    i0 = np.floor(idx).astype(int)
    fr = idx - i0
    i1 = np.clip(i0 + 1, 0, n - 1)
    wet = x[i0] * (1 - fr) + x[i1] * fr
    return x * (1 - mix * 0.5) + wet * mix


def mix_at(*parts):
    n = max(len(p) for p in parts)
    out = np.zeros(n)
    for p in parts:
        out[:len(p)] += p
    return out


def pad_to(x, d):
    n = secs(d)
    if len(x) >= n:
        return x[:n]
    return np.concatenate([x, np.zeros(n - len(x))])


# ---------------------------------------------------------------- finitura

def finish(x, hpf=25.0, max_len=None, gate_db=-62.0, peak=0.89):
    x = np.asarray(x, dtype=float)
    if PERCUSSION["on"]:
        gate_db = max(gate_db, -50.0)   # coda inudibile tagliata: meno sovrapposizioni
    if hpf:
        x = hp(x, hpf, order=2)
    if max_len is not None:
        x = x[: secs(max_len)]
    # taglia la coda silenziosa
    m = np.max(np.abs(x)) + 1e-12
    thr = m * 10 ** (gate_db / 20)
    above = np.nonzero(np.abs(x) > thr)[0]
    end = min(len(x), (above[-1] + secs(0.02)) if len(above) else len(x))
    x = x[:end]
    x = fade(x, 0.0, min(0.03, len(x) / SR * 0.2))
    m = np.max(np.abs(x)) + 1e-12
    return x * (peak / m)
