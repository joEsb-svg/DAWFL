# DAWFL

Un DAW per Android in stile "fruity loops", scritto in C++ con [JUCE](https://juce.com).

## Scaricare l'app

Ogni modifica sul branch `main` viene compilata automaticamente da GitHub Actions.
L'ultimo APK è sempre qui:

**https://github.com/joEsb-svg/DAWFL/releases/download/latest-build/DAWFL.apk**

Sul telefono: scaricalo, aprilo e consenti l'installazione da "origini sconosciute" quando richiesto.
Gli aggiornamenti si installano sopra la versione precedente.

## Stato attuale (v0.9)

**Suono e mix**
- 6 strumenti sintetizzati, 6 kit per strumento (Classic, Rock, Drum & Bass, Dubstep, Techno, Progressive)
- Editor del canale (tieni premuto il nome, o menu ▾ > Modifica suono): pitch, decay, tono, drive,
  humanize, volume, pan, filtro, riverbero, delay
- Uscita stereo, velocity che cambia anche il timbro, humanize (variazioni naturali a ogni colpo)
- Master: riverbero e delay a tempo condivisi, limiter con look-ahead, meter

**Campioni**
- Importa i tuoi suoni dal telefono (WAV, AIFF, FLAC, OGG, MP3): menu ▾ di un canale > Suono > Importa campione
- I campioni vengono copiati nell'app e normalizzati; pitch, decay, tono, drive ed effetti valgono anche per loro
- I progetti ricordano il campione di ogni canale

**Synth BassForge**
- Il synth BassForge (repository VST-, ramo bassforge-initial) e' compilato dentro l'app come settimo canale
- Suona dagli step (nota C3) o dal piano roll, con note on/off precise al campione; BPM e posizione arrivano
  al synth per LFO e Performer a tempo
- Interfaccia completa di BassForge a schermo intero (Adatta / Zoom), preset di fabbrica e utente
- Volume, pan, riverbero e delay del canale; lo stato del synth e' salvato nel progetto

**Piano roll**
- Note con inizio, durata e altezza per ogni canale e pattern (menu ▾ > Piano roll)
- Tocca per aggiungere o cancellare, trascina per spostare, trascina il bordo destro per allungare
- Griglia 1/4, 1/8, 1/16, 1/32 e terzine; zoom; il canale con note mostra l'anteprima al posto degli step
- Funziona con tutti gli strumenti, sintetizzati o campioni (es. linee di basso con un 808)

**Sequencer**
- 8 pattern da 16 o 32 step; il cambio pattern mentre suona avviene a fine giro
- Modalita' STEP, VEL (forza), PROB (probabilita'), ROLL (ratchet 1-4 colpi per step)
- Strumenti per riga: sposta, inverti, riempi, copia/incolla, svuota
- Undo / redo, swing, tap tempo (tocca il numero dei BPM), metronomo

**App**
- Icona, numero di versione nel menu FILE
- Vibrazione leggera al tocco (disattivabile in FILE > Impostazioni)
- Scelta del buffer audio per ridurre la latenza (FILE > Impostazioni)
- Layout orizzontale, verticale (8 step per pagina) e tablet

**Progetti**
- Nuovo, apri, salva, salva con nome, elimina; autosalvataggio e ripristino all'avvio

## Struttura

```
DAWFL.jucer                  progetto JUCE (Projucer)
Source/Main.cpp              avvio dell'app e finestra
Source/Engine/               motore audio (thread real-time), libreria suoni, salvataggio
Source/UI/                   interfaccia
BassForge/Source/            sorgenti del synth BassForge (JUCE AudioProcessor ospitato dal motore)
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
