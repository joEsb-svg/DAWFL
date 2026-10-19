# DAWFL

Un DAW per Android in stile "fruity loops", scritto in C++ con [JUCE](https://juce.com).

## Scaricare l'app

Ogni modifica sul branch `main` viene compilata automaticamente da GitHub Actions.
L'ultimo APK è sempre qui:

**https://github.com/joEsb-svg/DAWFL/releases/download/latest-build/DAWFL.apk**

Sul telefono: scaricalo, aprilo e consenti l'installazione da "origini sconosciute" quando richiesto.
Gli aggiornamenti si installano sopra la versione precedente.

## Stato attuale (v0.3)

- Motore audio a bassa latenza (Oboe, tramite JUCE)
- Channel rack con 6 strumenti sintetizzati: Kick, Snare, Clap, Hi-Hat, Open Hat, Tom
- Libreria suoni: ogni strumento ha 6 varianti (Classic, Rock, Drum & Bass, Dubstep, Techno, Progressive),
  scegli dalla freccia accanto al nome oppure cambia tutto insieme dal pulsante KIT
- Pad a 16 step "dipingibili" (tocca e trascina), mute e volume per canale
- Tocca il nome di un canale per sentirne il suono
- BPM: tocca - / + oppure trascina su/giu' sul display
- Volume master con saturazione morbida e meter d'uscita

## Struttura

```
DAWFL.jucer                  progetto JUCE (Projucer)
Source/Main.cpp              avvio dell'app e finestra
Source/Engine/               motore audio (thread real-time)
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
