"""
Genera la libreria suoni di JLoop:
  - Resources/Library/<id>.ogg            (mono, 44.1 kHz, Ogg Vorbis q6)
  - Source/Engine/LibraryIndex.h          (elenco per il codice C++)
  - aggiorna il gruppo "Library" in JLoop.jucer (i file diventano BinaryData)

Uso:  python3 tools/soundgen/generate.py [id ...]     (senza id: tutti)
"""
import os
import sys
import subprocess
import multiprocessing as mp
import numpy as np
from scipy.io import wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

import dsp            # noqa: E402
import instruments    # noqa: E402,F401
import electronic     # noqa: E402,F401
from registry import SOUNDS  # noqa: E402

OUT = os.path.join(ROOT, "Resources", "Library")


def render(index):
    entry = SOUNDS[index]
    x = np.asarray(entry["fn"](), dtype=float)
    if not np.all(np.isfinite(x)):
        raise RuntimeError(entry["id"] + ": valori non validi")
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype(np.int16)
    wav = os.path.join(OUT, entry["id"] + ".wav")
    flac = os.path.join(OUT, entry["id"] + ".ogg")
    wavfile.write(wav, dsp.SR, pcm)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", wav, "-c:a", "libvorbis", "-q:a", "6", flac], check=True)
    os.remove(wav)
    return entry["id"], len(x) / dsp.SR, os.path.getsize(flac)


def cpp_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_index():
    lines = [
        "#pragma once",
        "// File generato da tools/soundgen/generate.py: non modificare a mano.",
        "",
        "namespace LibraryIndex",
        "{",
        "    struct Entry",
        "    {",
        "        const char* id;          // salvato nei progetti come \"lib:<id>\"",
        "        const char* name;        // nome mostrato",
        "        const char* group;       // Strumenti / Elettronica",
        "        const char* category;",
        "        const char* resource;    // nome in BinaryData",
        "        int   rootNote;          // nota MIDI a cui e' registrato",
        "        bool  gated;             // nel piano roll la nota si ferma alla sua fine",
        "        float release;           // secondi di sfumatura a fine nota",
        "        unsigned int colour;",
        "    };",
        "",
        "    inline constexpr Entry entries[] =",
        "    {",
    ]
    for s in SOUNDS:
        res = s["id"].replace("-", "_") + "_ogg"
        col = s["colour"] if s["colour"] is not None else 0xff8aa0b8
        lines.append("        { %s, %s, %s, %s, %s, %d, %s, %.3ff, 0x%08xu }," % (
            cpp_string(s["id"]), cpp_string(s["name"]), cpp_string(s["group"]), cpp_string(s["category"]),
            cpp_string(res), s["root"], "true" if s["gated"] else "false", s["release"], col))
    lines += [
        "    };",
        "",
        "    inline constexpr int numEntries = (int) (sizeof (entries) / sizeof (entries[0]));",
        "}",
        "",
    ]
    with open(os.path.join(ROOT, "Source", "Engine", "LibraryIndex.h"), "w") as f:
        f.write("\n".join(lines))


def update_jucer():
    path = os.path.join(ROOT, "JLoop.jucer")
    text = open(path).read()
    start_tag = '<GROUP id="{LIBRARY-0000-0000-0000-000000000001}" name="Library">'
    files = "".join(
        '\n        <FILE id="Lb%04d" name="%s.ogg" compile="0" resource="1" file="Resources/Library/%s.ogg"/>' % (i, s["id"], s["id"])
        for i, s in enumerate(SOUNDS))
    group = start_tag + files + "\n      </GROUP>"
    if start_tag in text:
        a = text.index(start_tag)
        b = text.index("</GROUP>", a) + len("</GROUP>")
        text = text[:a] + group + text[b:]
    else:
        # dentro il gruppo Resources
        anchor = 'name="Resources">'
        i = text.index(anchor) + len(anchor)
        text = text[:i] + "\n      " + group + text[i:]
    open(path, "w").write(text)


def main():
    os.makedirs(OUT, exist_ok=True)
    ids = set(sys.argv[1:])
    ids_all = [s["id"] for s in SOUNDS]
    assert len(ids_all) == len(set(ids_all)), "id duplicati"
    todo = [i for i, s in enumerate(SOUNDS) if not ids or s["id"] in ids]
    with mp.Pool(max(1, os.cpu_count() or 1)) as pool:
        results = pool.map(render, todo, chunksize=1)
    total = 0
    for sid, dur, size in results:
        total += size
        print("%-22s %5.2fs %7.1f KB" % (sid, dur, size / 1024))
    if not ids:
        # rimuove file di suoni non piu' in elenco
        for f in os.listdir(OUT):
            if f.endswith(".flac") or (f.endswith(".ogg") and f[:-4] not in ids_all):
                os.remove(os.path.join(OUT, f))
        write_index()
        update_jucer()
    print("suoni: %d   totale: %.2f MB" % (len(results), sum(r[2] for r in results) / 1048576))


if __name__ == "__main__":
    main()
