# BassForge su Windows

## Cosa c'è
| File | Cos'è |
|---|---|
| `BassForge.exe` | **Applicazione standalone** (Windows 10/11 x64). Doppio click, scegli la scheda audio in *Options → Audio/MIDI Settings*, suona con una tastiera MIDI. |
| `BassForge.vst3\` | **Plugin VST3** per DAW (Ableton, FL Studio, Reaper, Cubase...). Copia l'intera cartella in `C:\Program Files\Common Files\VST3\`. |

Un `.vst3` non è un `.exe`: è una DLL che si carica dentro una DAW. L'`.exe` è la versione standalone.

## Compilarlo da sorgente
**Con MSVC (consigliato, percorso ufficiale JUCE).** Serve CMake, Git e *Visual Studio 2022* o
*Build Tools for Visual Studio 2022* con il carico "Sviluppo di applicazioni desktop con C++":

    build_windows.bat

Oppure su GitHub: `.github/workflows/windows.yml` compila e pubblica `.exe` e `.vst3` come artefatti.

**Cross-compilazione da Linux** (così sono stati generati i file di questa cartella): vedi
`build_windows_crosscompile.sh` e `cmake/juce-8.0.8-llvm-mingw.patch` (JUCE 8 non supporta MinGW ufficialmente:
la patch sistema 8 punti minori. Per MSVC non serve).

## Verifica effettuata e limiti
- Compilazione e link riusciti (clang/llvm-mingw 19, UCRT, runtime C++ linkato staticamente): l'exe è un PE32+ GUI x86-64
  che importa solo DLL di sistema Windows (kernel32, user32, d2d1, dxgi, ...). Il VST3 esporta `GetPluginFactory`/`InitDll`/`ExitDll`.
- Il DSP è identico alla build Linux: 10 suite di test PASS e render dei 5 preset a −0.30 dBFS di picco.
- **L'exe non è stato avviato su Windows** (nel server di build non c'è Windows né Wine): GUI, scheda audio e
  caricamento nella DAW vanno provati sul tuo PC. Il VST3 cross-compilato non include il manifest `moduleinfo.json`
  (opzionale, serve solo a velocizzare la scansione); la build MSVC lo genera.
- Il percorso MSVC (`build_windows.bat`) non è stato eseguito qui.
