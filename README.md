# Fallout Babylon

Éditeur de traduction desktop (C++17 / Qt 6 / Scintilla) pour les fichiers de dialogues et de textes
(`.msg`) des jeux et mods **Fallout 1 et Fallout 2**.

## État d'avancement (MVP, étapes 1 à 4)

| Étape | Contenu | État |
|---|---|---|
| 1 | Multi-projets, dossiers récents, lecture/écriture `.msg`, DAT1/DAT2 | fait |
| 2 | Éditeur + vue linéaire source/cible, sauvegarde, encodages | fait |
| 3 | Glossaire depuis `/game`, soulignement, suggestions, alertes d'incohérence | fait |
| 4 | Statistiques (%, mots, caractères, lignes, personnages/fichiers), filtres | fait |
| 5 | Statut « à revoir » + liste TODO | fait (marquage Ctrl+R, filtre, panneau « À revoir » avec ouverture au double-clic) |
| 6 | Décompilation INT (int2ssl) + vue arbre | à faire (`tools/int2ssl.exe` est déjà là) |
| 7 | DeepL + mode masse | à faire |
| 8 | Grammalecte / LanguageTool, liaison jeu → TODO | à faire |

### Ce qui est en place

* **Projets** : plusieurs projets ouverts en parallèle (un nœud par projet dans la vue Projets), chacun avec ses
  sources (archives `.dat`, dossiers de jeu ou de mod, empilés par priorité comme dans le jeu), sa langue source/cible,
  ses encodages, son dossier de sortie, son glossaire et son avancement. Les projets ouverts et les dossiers récents sont
  mémorisés. La découverte d'un dossier de jeu reconnaît `master.dat`, `critter.dat`, les patches, `data/` et l'ordre
  `mods/mods_order.txt` de sfall (RPU, Fallout1in2…).
* **Archives** : lecture DAT1 (LZSS) et DAT2 (zlib), écriture DAT1 (non compressé) et DAT2 (zlib), copie brute d'entrées
  entre archives. Export des fichiers traduits vers une archive (menu Fichier ou clic droit sur le projet).
* **Fichiers `.msg`** : analyse fidèle au moteur (tout ce qui est hors accolades est conservé : un fichier non modifié se
  réécrit octet pour octet), retours à la ligne CRLF/LF conservés. Une ligne non traduite est écrite avec le texte
  source pour que le jeu n'affiche jamais de ligne vide.
* **Encodages** : CP1252, CP1250, CP1251 (tables internes, identiques sur toutes les plateformes), Big5, GBK, Shift-JIS,
  EUC-KR (convertisseur de la plateforme), UTF-8. Les caractères non représentables sont signalés à l'enregistrement.
* **Ordre conseillé** : la vue Projets range les fichiers en « 1 · Fichiers du jeu — à faire en premier »,
  « 2 · Dialogues », « 3 · Autres », avec le rappel que `/game` alimente le glossaire.
* **Glossaire** : construit automatiquement depuis `text/<langue>/game/*.msg` (noms des objets, créatures, scénery, compétences,
  traits, lieux, quêtes…). Termes soulignés dans la source et dans la traduction (trait plein = correspondance exacte,
  pointillé = variante à une lettre près), info-bulle au survol, menu contextuel « insérer la traduction », alerte ⚠ quand
  un terme de la source n'est pas rendu par sa traduction connue, détection des termes qui ont plusieurs traductions
  différentes dans les fichiers du jeu. Pluriels, possessifs, casse et élisions (l'armure) sont gérés.
* **Statistiques** : par fichier, par catégorie et globales — % traduit, lignes, mots et caractères restants, nom du
  personnage (déduit du fichier). Calcul en tâche de fond ; filtres « non traduits / partiels / terminés / à revoir ».
* **Interface** : thème clair / sombre / système, interface traduite (français complet ; les autres langues retombent sur
  l'anglais en attendant leurs traductions, voir « i18n » plus bas).

### Raccourcis

`Ctrl+S` enregistrer · `Ctrl+Maj+S` tout enregistrer · `Ctrl+↓/↑` ligne suivante/précédente ·
`Ctrl+Maj+↓/↑` prochaine/précédente ligne non traduite · `Ctrl+Maj+C` copier la source ·
`Ctrl+Entrée` valider tel quel · `Ctrl+R` marquer « à revoir » · `Ctrl+F` rechercher · `F5` actualiser les statistiques.

### Comment un segment est jugé « traduit »

Une ligne est *non traduite* si sa traduction est vide, ou identique à la source sans avoir été validée. Écrire
exactement le texte source (un nom propre, « Stimpak ») ou utiliser **Valider tel quel** la marque comme traduite. Les
validations et les marques « à revoir » sont stockées à côté du projet (`%APPDATA%/FalloutBabylon/projects/<id>.state.json`).

## Compilation

Prérequis : Visual Studio 2022+ (MSVC), CMake, Ninja, Qt 6.5+ (testé avec 6.8.3, modules Widgets et Core5Compat). Scintilla 5.6.7
est fourni dans `third_party/scintilla` (port Qt `ScintillaEditBase`, lié statiquement).

```powershell
.\build.ps1                 # build Release dans .\build
.\build.ps1 -Test           # + tests unitaires du noyau
.\build.ps1 -Deploy         # + dossier autonome dans .\dist\FalloutBabylon (windeployqt)
```

Variables utiles : `-Qt D:\Qt\6.8.3\msvc2022_64` si Qt est ailleurs.

### Tests

* `babylon_tests` : noyau (LZSS, `.msg`, codages, DAT1/DAT2 écriture→lecture, termes, glossaire, projet). Avec
  `BABYLON_SAMPLE_INSTALL=C:\Fallout2\...` ils s'exécutent aussi sur une vraie installation (lecture seule) ;
  `BABYLON_SAMPLE_DAT` et `BABYLON_SAMPLE_FOLDER` permettent de tester un DAT ou un dossier `text/` précis.
* `babylon_gui_smoke` : fenêtre complète sur un petit jeu de données (ouverture, édition, alertes, sauvegarde, thèmes) ;
  `BABYLON_SHOT_DIR=...` écrit des captures d'écran, `BABYLON_QM=...babylon_fr.qm` teste une traduction,
  `BABYLON_PROFILE=1` signale les sections lentes du thread d'interface.

### Icônes

Les icônes viennent de `_icons/` : `icon.jpg` pour 16 à 64 px, `icon_hd.jpg` pour 128 à 512 px.
`python tools/make_icons.py` (Pillow) régénère `resources/icons/*.png` (embarqués pour l'icône de fenêtre) et
`resources/app.ico` (icône de l'exécutable, 16 à 256 px : un `.ico` ne dépasse pas 256 px).

## Organisation

```
src/core   noyau sans interface : DAT, LZSS, .msg, codages, VFS, projets, glossaire, statistiques
src/ui     fenêtres, panneaux, thème, enveloppe Scintilla
i18n       fichiers .ts (Qt Linguist) ; tools/translations/<code>.py + tools/fill_ts.py les remplissent
tests      tests unitaires et test de fumée de l'interface
third_party/scintilla   Scintilla (licence HPND)
```

### i18n

Les chaînes sources sont en anglais. Pour mettre à jour : `lupdate src -ts i18n/*.ts -no-obsolete`, puis compléter
`tools/translations/<code>.py` et lancer `python tools/fill_ts.py <code>`. Seul `fr` est traduit pour l'instant ; le menu
Affichage > Langue ne propose que les langues dont la traduction est présente.

## Limites connues et suites prévues

* **Exécutable unique** : le build actuel utilise les DLL de Qt (dossier `dist\FalloutBabylon`, ~29 Mo). Un `.exe` unique
  demande de compiler Qt en statique (non fourni par les installeurs officiels) : à faire une fois le périmètre stabilisé.
* DAT1 : la décompression LZSS suit l'algorithme documenté (et l'implémentation de DatExplorerIII) mais n'a pu être testée
  que sur des flux synthétiques, aucun DAT1 compressé n'étant disponible ici. DAT2 est validé sur de vraies archives.
* « Navigation virtuelle dans les archives » : la vue Projets présente les fichiers `.msg` des sources fusionnées ;
  un navigateur d'archive complet (tous types de fichiers) n'est pas encore fait.
* Le glossaire n'a pas encore d'entrées saisies à la main (il est dérivé des fichiers `/game`).
* Les fichiers `.msg` multi-lignes : le moteur ignore les retours à la ligne à l'intérieur d'un champ ; ils sont ici conservés
  tels quels dans le texte.
