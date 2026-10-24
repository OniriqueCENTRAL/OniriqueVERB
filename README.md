# ONIRIQUE VERB

Reverb pour la voix, VST3 (Windows, Mac) et AU (Mac), sans latence.

## Ce qu'elle fait

- **Six espaces** : ROOM, PLATE I (brillante), PLATE II (sombre et large), HALL, SHIMMER (octave aérienne),
  ONIRIQUE (salle immense et sombre, modulée en profondeur, voile d'octave).
- **Le moteur** : 16 lignes à retards, six passe-tout courts à l'entrée et un dans chaque boucle (la queue est dense
  en 25 à 80 ms : pas de grain sur les consonnes), graves qui s'éteignent avant les médiums selon l'espace, entrée
  stéréo (une source à gauche le reste au début de la queue), modulation sinusoïdale et dérive au hasard.
- **L'affichage** : l'espace vu de face. Chaque anneau est un instant de la queue qui s'enfonce dans la
  profondeur, la voix sèche passe devant en ruban. Glisser dedans règle SIZE (gauche / droite) et DECAY
  (haut / bas), double-clic pour revenir aux valeurs d'origine, Maj pour un réglage fin.
- **ESPACE** : PRE-DELAY, DECAY, SIZE, DIFFUSION. **MOUVEMENT** : MOD RATE, MOD DEPTH, WIDTH, OUTPUT.
  Sur chaque potard : Maj pour un réglage fin, double-clic pour la valeur d'origine, clic sur la valeur pour la taper.
- **MIX** : la voix reste entière jusqu'à 50 %, la reverb monte ; au-delà la voix s'efface. Le cadenas
  garde le MIX quand on change de preset.
- **EQ** : EQ IN (ce qui entre dans la reverb) et EQ OUT (la queue), chacun avec coupe-bas, grave, médium, aigu
  et coupe-haut à tirer à la souris, sur le spectre en direct de ce qu'il règle ; DUCKING (la reverb s'efface sous la
  voix), CLEAN (sifflantes retirées, bas-médiums creusés, ancienne queue effacée à chaque nouvelle phrase),
  PULSE (la queue respire au tempo du DAW), INFINITE (fige le feedback : la queue tient indéfiniment).
- **SYNC** (en-tête) : allumé, DECAY (d'un temps à 8 mesures) et PRE-DELAY (de 0 à une noire) se règlent en
  valeurs de notes, calculées au tempo. Le tempo suit le DAW, ou se choisit (glisser sur le chiffre, double-clic
  pour le taper, menu DAW / MAN). PULSE et STUTTER suivent ce tempo.
- **STUTTER** (panneau RYTHME) : découpe la queue au tempo. CHOP (hachée net), REPEAT (le premier morceau de chaque
  groupe de quatre rejoué trois fois), RAMP (les coupes accélèrent à chaque temps de la mesure), avec division et
  dosage. La voix sèche n'est jamais découpée. Le motif s'affiche avec sa tête de lecture.
- Presets d'usine et presets perso (`%APPDATA%\Onirique\Reverb\Presets`).

## Compiler (Windows)

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target OniriqueReverb_VST3
```

Le plugin sort dans `build\OniriqueReverb_artefacts\Release\VST3\ONIRIQUE VERB.vst3`.
JUCE est pris dans `..\_juce\8.0.6` s'il existe, sinon téléchargé.

Version d'essai sans licence (nom « ONIRIQUE VERB TEST », s'installe à côté de la vraie) :
`cmake -B build-test -DONIRIQUE_TEST_BUILD=ON`.

## Outils

- `OniriqueReverbEngineTest [voix.wav [dossier]]` : banc d'essai du moteur (durées de queue, INFINITE,
  stabilité, CLEAN, PULSE, DUCKING, STUTTER, EQ, densité, graves, entrée stéréo, clics, charge) ; avec une voix,
  écrit un fichier par mode.
- `OniriqueReverbSnapshot <image.png> [echelle] [mode 0-5] [infinite]` : image de l'interface, sans DAW.

## Licence

Identifiant ONIRIQUE CENTRAL : `onirique-verb` (voir `License/README.md`). Le produit doit exister dans
ONIRIQUE CENTRAL sous cet identifiant pour que l'activation fonctionne.
