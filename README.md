# DAWFL

Un DAW per Android in stile "fruity loops", scritto in C++ con [JUCE](https://juce.com).

## Scaricare l'app

Ogni modifica sul branch `main` viene compilata automaticamente da GitHub Actions.
L'ultimo APK è sempre qui:

**https://github.com/joEsb-svg/DAWFL/releases/download/latest-build/DAWFL.apk**

Sul telefono: scaricalo, aprilo e consenti l'installazione da "origini sconosciute" quando richiesto.
Gli aggiornamenti si installano sopra la versione precedente.

## Stato attuale (v0.4)

- Motore audio a bassa latenza (Oboe, tramite JUCE)
- Channel rack con 6 strumenti sintetizzati: Kick, Snare, Clap, Hi-Hat, Open Hat, Tom
- Libreria suoni: 6 kit per strumento (Classic, Rock, Drum & Bass, Dubstep, Techno, Progressive)
- 8 pattern per progetto, lunghezza 16 o 32 step; copia, svuota, cambio al volo
- Modalita' STEP (accendi/spegni, trascina per dipingere) e VEL (forza di ogni colpo)
- Swing globale, BPM, volume master con saturazione morbida, meter
- Progetti: nuovo, apri, salva, salva con nome, elimina (menu FILE)
- Autosalvataggio: alla riapertura ritrovi il lavoro esattamente com'era

## Struttura

```
DAWFL.jucer                  progetto JUCE (Projucer)
Source/Main.cpp              avvio dell'app e finestra
Source/Engine/               motore audio (thread real-time), libreria suoni, salvataggio
Source/UI/                   interfaccia
.github/workflows/           compilazione automatica dell'APK
ci/debug.keystore            chiave di firma di sviluppo (password "android")
```

## Compilare sul PC (opzionale)

1. Clona JUCE 9.0.4 dentro la cartella del progetto: `git clone --branch 9.0.4 https://github.com/juce-framework/JUCE.git JUCE`
2. Apri `DAWFL.jucer` con il Projucer e salva: genera i progetti in `Builds/`
3. Windows: apri `Builds/VisualStudio2022`; Android: apri `Builds/Android` con Android Studio

## Licenza JUCE

JUCE è disponibile con licenza AGPLv3 oppure con licenza commerciale.
Se un giorno l'app verrà distribuita o venduta, va verificata la licenza adatta.
