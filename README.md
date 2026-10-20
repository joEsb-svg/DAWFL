# JLoop

(ex DAWFL)

Un DAW per Android in stile "fruity loops", scritto in C++ con [JUCE](https://juce.com).

## Scaricare l'app

Ogni modifica sul branch `main` viene compilata automaticamente da GitHub Actions.
L'ultimo APK è sempre qui:

**https://github.com/joEsb-svg/JLoop/releases/download/latest-build/JLoop.apk**

Sul telefono: scaricalo, aprilo e consenti l'installazione da "origini sconosciute" quando richiesto.
Gli aggiornamenti si installano sopra la versione precedente.

## Stato attuale (v0.21)

**Canali (channel rack)**
- Fino a 32 canali: "+ Strumento" aggiunge un canale scegliendo il suono dal browser
- Tieni premuto il nome di un canale (o menu ▾) per il menu: cambia suono, modifica suono e mix, piano roll,
  muto, solo, clona, rinomina, colore, sposta su/giu', strumenti per gli step, elimina
- Tocca il nome per ascoltare; trascina in verticale sul nome per scorrere la lista

**Libreria suoni (193 suoni, generati da zero con tools/soundgen)**
- Strumenti: pianoforti e tastiere, chitarre, bassi, archi, fiati e ottoni, voci e cori,
  percussioni melodiche, batteria acustica, percussioni
- Elettronica: EDM, House, Techno, Electro, Drum and Bass, Dubstep, Trap, synth e pad, effetti (FX),
  Drum machine (suoni in stile 808, 909 e Lo-Fi)
- Browser con anteprima: tocca un suono per sentirlo, poi "Aggiungi" o "Usa questo suono"
- Campionatore polifonico (accordi nel piano roll); gli strumenti "tenuti" (archi, organo, pad...) sfumano
  alla fine della nota; ogni suono sa la sua nota, cosi' il piano roll suona intonato

**Mixer** (pulsante MIXER, o MENU > Mixer)
- Traccia Master + 32 tracce insert, come in FL Studio: ogni canale manda il suono a una traccia
  (menu del canale > Mixer); i canali nuovi prendono da soli una traccia libera
- Per traccia: volume (fader in dB), panorama, muto, solo, misuratore, nome e colore
- Equalizzatore a 3 bande (bassi, medi con la frequenza, alti) e fino a 6 effetti in fila:
  compressore, saturazione, filtro (con LFO), delay a tempo, riverbero, chorus, bitcrusher,
  sidechain, stereo; ogni effetto si puo' escludere o spostare su e giu'
- Sidechain: la traccia si abbassa a ogni colpo di un'altra (la cassa) e risale nel tempo di rilascio
- Anche la traccia Master ha EQ ed effetti (prima del limiter); tutto e' salvato nel progetto e nell'undo

**Mastering** (MENU > Esporta audio)
- "Automatico": JLoop calcola la canzone, misura il volume percepito (LUFS, come Spotify e YouTube),
  l'equilibrio tra bassi e alti, la dinamica e l'apertura stereo, e regola tutto per lo stile scelto
- 6 stili: Bilanciato, Streaming (-14 LUFS), Club / EDM, Hip hop / Trap, Caldo, Brillante
- Poi si cambia a mano: volume finale (LUFS), bassi, presenza, aria, compressione, calore, stereo, tetto
- Catena: EQ (sotto i 25 Hz si taglia), compressione "colla", saturazione, bassi in mono,
  clipper morbido e limiter con anticipo (i picchi non superano mai il tetto)
- "Ascolta" fa partire la canzone: accendi e spegni il mastering per confrontare; vale anche dal vivo
- Le impostazioni sono salvate nel progetto e usate dall'esportazione

**Pattern**
- 99 pattern per progetto: dal menu del pattern "Nuovo pattern vuoto", "Clona in un pattern nuovo",
  "Rinomina", "Copia in", "Svuota"; le frecce scorrono i pattern in uso piu' uno nuovo
- I nomi dei pattern compaiono anche nella playlist

**Suono e mix**
- Batteria sintetizzata: 6 strumenti, 6 kit (Classic, Rock, Drum & Bass, Dubstep, Techno, Progressive)
- Editor del canale (menu del canale > Modifica suono e mix): pitch, decay, tono, drive,
  humanize, volume, pan, filtro, riverbero, delay
- Uscita stereo, velocity che cambia anche il timbro, humanize (variazioni naturali a ogni colpo)
- Master: riverbero e delay a tempo condivisi, limiter con look-ahead, meter

**Campioni**
- Importa i tuoi suoni dal telefono (WAV, AIFF, FLAC, OGG, MP3): browser dei suoni > Campioni > Importa
- I campioni vengono copiati nell'app e normalizzati; pitch, decay, tono, drive ed effetti valgono anche per loro
- I progetti ricordano il campione di ogni canale

**Strumenti (synth)**
- Ogni canale strumento ha il suo synth: si possono avere piu' BassForge e piu' PolyForge insieme
  (browser dei suoni > Sintesi > Synth: BassForge, PolyForge, FMForge, DrumForge); clona, sposta ed elimina come gli altri canali
- Pannello dello strumento con tastiera a schermo per suonare e provare i preset
- Preset di fabbrica e utente per ogni synth ("Salva come preset utente" dal menu Preset)

**Synth PolyForge** (Source/Instruments/PolyForge)
- Polifonico (12 voci) o Mono / Legato con glide: pad, lead, plucks, accordi, stab
- 2 oscillatori (saw, quadra/PWM, triangolo, sinusoide) con unison fino a 7 voci, sub, rumore, stereo
- Filtro LP 24 / LP 12 / HP / passa-banda con risonanza, drive, keytrack, inviluppo e velocity
- 2 inviluppi ADSR, LFO (5 forme, libero o a tempo) su vibrato, filtro, volume e PWM
- Effetti: chorus, delay ping-pong a tempo, riverbero; 24 preset di fabbrica
- Interfaccia per il telefono nello stile di BassForge: schede OSC, FILTRO, ENV, LFO, FX

**Drum machine DrumForge** (Source/Instruments/DrumForge)
- 16 pad suonati dalle note 36..51 (gli step del channel rack suonano il pad 1); nel piano roll ogni riga
  ha il nome del suo pad
- Per ogni pad: suono (libreria o campioni importati), intonazione, decadimento, volume, panorama, filtro,
  riverbero, velocity e gruppo choke (l'hi-hat chiuso zittisce quello aperto)
- 12 kit: Acustico, Acustico secco, 808, 909, House, Techno, Trap, Drum and Bass, Dubstep, EDM, Lo-Fi,
  Percussioni; kit dell'utente ("Salva come preset utente"); saturazione e riverbero in uscita
- Interfaccia: scheda PAD (tocca un pad per suonarlo e modificarlo) e scheda KIT
- Browser dei suoni > Sintesi: "Kit DrumForge" (tutto il kit in un canale) o "Kit su canali separati"
  (cassa, rullante, clap, hat, tom e piatto, un canale ciascuno)

**Synth FMForge** (Source/Instruments/FMForge)
- FM a 4 operatori (stile DX/TX): rapporto, fine, livello, velocity e ADSR per operatore
- 8 algoritmi (disegnati nella scheda ALGO), feedback, "Timbro", filtro "Tono"
- LFO su vibrato, tremolo e timbro; Poly / Mono / Legato con glide; chorus, delay, riverbero
- 22 preset: piani elettrici, organo, clavinet, campane, glockenspiel, marimba, vibrafono,
  kalimba, steel drum, bassi FM (anche slap, 80s, growl, sub), brass, lead, pad, pluck, metalli

**Synth BassForge**
- Il synth BassForge (repository VST-, ramo bassforge-initial) e' compilato dentro l'app (un canale per progetto)
- Suona dagli step (nota C3) o dal piano roll, con note on/off precise al campione; BPM e posizione arrivano
  al synth per LFO e Performer a tempo
- Interfaccia originale di BassForge adattata al telefono: una sezione alla volta (OSC 1/2/3, Sub/Noise,
  Sample, Filtri, Env/LFO/Macro/Matrice/Performer, FX)
- Preset di fabbrica e utente
- 19 preset di fabbrica per genere (Rock, DnB, Dubstep, Techno, Progressive), con archetipi in stile Massive:
  hoover, scream lead, sequenza col Performer, supersaw
- Volume, pan, riverbero e delay del canale; lo stato del synth e' salvato nel progetto

**Piano roll**
- Note con inizio, durata e altezza per ogni canale e pattern (menu ▾ > Piano roll)
- Tocca per aggiungere o cancellare, trascina per spostare, trascina il bordo destro per allungare
- Griglia 1/4, 1/8, 1/16, 1/32 e terzine; zoom; il canale con note mostra l'anteprima al posto degli step
- Funziona con tutti gli strumenti, sintetizzati o campioni (es. linee di basso con un 808)

**Playlist (canzone)**
- Interruttore PAT / SONG accanto al play: PAT ripete il pattern selezionato, SONG suona la playlist
- 12 tracce, fino a 256 battute: tocca per mettere il pattern scelto (pennello), tocca una clip per toglierla,
  trascinala per spostarla, tienila premuta per il menu (modifica pattern, cambia, duplica, elimina)
- Piu' pattern possono suonare insieme; tocca il righello per far partire la canzone da una battuta
- La playlist e' salvata nel progetto

**Esporta audio**
- FILE > Esporta audio: pattern selezionato (1-8 ripetizioni) o canzone intera, in WAV (qualita' CD) o OGG
- Calcolo offline in un thread a parte (piu' veloce del tempo reale), con la coda di riverbero e delay
- Poi "Condividi" (WhatsApp, Drive, mail...) o "Salva nel telefono"

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
JLoop.jucer                  progetto JUCE (Projucer)
Source/Main.cpp              avvio dell'app e finestra
Source/Engine/               motore audio (thread real-time), campionatore, libreria suoni, salvataggio
Resources/Library/           i suoni della libreria (Ogg Vorbis, generati da tools/soundgen)
tools/soundgen/              script Python che sintetizza la libreria (python3 tools/soundgen/generate.py)
Source/UI/                   interfaccia
BassForge/Source/            sorgenti del synth BassForge (JUCE AudioProcessor ospitato dal motore)
.github/workflows/           compilazione automatica dell'APK
ci/debug.keystore            chiave di firma di sviluppo (password "android")
```

## Compilare sul PC (opzionale)

1. Clona JUCE 9.0.4 dentro la cartella del progetto: `git clone --branch 9.0.4 https://github.com/juce-framework/JUCE.git JUCE`
2. Apri `JLoop.jucer` con il Projucer e salva: genera i progetti in `Builds/`
3. Windows: apri `Builds/VisualStudio2022`; Android: apri `Builds/Android` con Android Studio

## Licenza JUCE

JUCE è disponibile con licenza AGPLv3 oppure con licenza commerciale.
Se un giorno l'app verrà distribuita o venduta, va verificata la licenza adatta.
