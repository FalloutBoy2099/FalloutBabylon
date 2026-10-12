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
| 6 | Décompilation INT (int2ssl) + vue arbre | fait (panneau « Arbre de dialogue », voir plus bas) |
| 7 | DeepL + mode masse | fait (voir « Traduction automatique ») |
| 8 | Grammalecte / LanguageTool, liaison jeu → TODO | correction fait ; liaison jeu → TODO (sfall) pas commencée |

### Ce qui est en place

* **Projets** : plusieurs projets ouverts en parallèle (un nœud par projet dans la vue Projets), chacun avec ses
  sources (archives `.dat`, dossiers de jeu ou de mod, empilés par priorité comme dans le jeu), sa langue source/cible,
  ses encodages, son dossier de sortie, son glossaire et son avancement. Les projets ouverts et les dossiers récents sont
  mémorisés. La découverte d'un dossier de jeu reconnaît `master.dat`, `critter.dat`, les patches, `data/` et l'ordre
  `mods/mods_order.txt` de sfall (RPU, Fallout1in2…). **Un répertoire nommé `*.dat` est traité comme l'archive du même
  nom** : `master.dat\`, `critter.dat\`, `f2_res.dat\` ou `mods\rpu.dat\` extraits dans des dossiers fonctionnent
  partout (découverte, priorités, mods) comme les fichiers `.dat`.
* **Dossier de sortie** (où sont écrits `text/<langue>/...`, prioritaire sur les sources) : par défaut le dossier `data`
  de l'installation ; sinon le dossier d'un mod qui n'est pas un répertoire `*.dat` ; sinon un dossier à part dans
  Documents. Un répertoire `*.dat` extrait n'est jamais choisi : il représente une archive et reste intact. Le dialogue
  affiche le chemin exact des fichiers écrits.
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
* **Interface** : thème clair / sombre / système, interface traduite en FR, EN, ES, IT, PT, DE, PL, CS, RU et ZH (choix
  dans Affichage > Langue, appliqué au prochain démarrage).

### Traduction automatique (DeepL) et correction

Outils > Paramètres : clé d'API DeepL (stockée dans les réglages de l'utilisateur, en clair : ne partagez pas ce fichier),
registre, options, et adresses des serveurs de correction.

* **DeepL** (API REST, hôte gratuit ou Pro selon la clé) : `Ctrl+T` traduit la ligne courante, `Ctrl+Maj+T` ouvre la
  traduction de masse (lignes non traduites / sélectionnées / toutes, estimation des caractères et du quota restant,
  confirmation avant envoi, propositions modifiables et décochables dans un tableau, application en bloc). Chaque ligne
  part avec un contexte (nom du dialogue et lignes voisines, non facturé) et, si possible, le glossaire du projet (glossaire
  DeepL v3 reconstruit quand le glossaire change). Les jetons du jeu (`@Nom@`, `%d`, `{champ}`) sont protégés ; une
  ligne dont un jeton a été altéré est signalée et décochée. Les traductions automatiques sont marquées « à revoir ».
  Le plan gratuit n'autorise qu'un glossaire : s'il est déjà pris, la traduction continue sans glossaire.
* **Correction** : Grammalecte (français) ou LanguageTool (autres langues) via leurs serveurs HTTP. Le texte de la
  traduction est vérifié après la frappe ; erreurs d'orthographe en rouge, de grammaire/style en violet, bulle au survol,
  remplacement par clic droit. Le texte de la ligne en cours part vers ces serveurs : utilisez des serveurs de confiance.

### Arbre de dialogue

Pour un fichier `dialog/<nom>.msg`, le panneau « Arbre de dialogue » retrouve `scripts/<nom>.int` dans les sources,
le décompile avec `tools/int2ssl.exe` (copié à côté de l'exécutable), lit les appels `gsay_reply`, `gsay_message`,
`giq_option`/`gsay_option` et `call`, et affiche la conversation : réplique du PNJ → options du joueur → procédure
suivante. Une procédure déjà affichée devient un renvoi « voir plus haut » (clic = saut à la première occurrence). Un clic
sur un nœud sélectionne la ligne dans l'éditeur, et inversement ; les coches suivent l'avancement de la traduction.
Limites : l'arbre suit la structure du script, pas ses conditions (une option conditionnelle apparaît toujours) ; une
numérotation de message calculée (`gsay_reply(f, id + 1)`) n'est pas résolue ; un script sans `talk_p_proc` n'affiche que
les procédures non appelées qui contiennent du dialogue.

### Holodisques du Pip-Boy (`game/pipboy.msg`)

Le moteur affiche **une entrée du `.msg` par ligne d'écran**, sans retour à la ligne automatique : le corps d'un holodisque
est une suite d'identifiants consécutifs (à partir de l'id « text » de `data/holodisk.txt`) qui se termine par une entrée
`**END-DISK**` ; une entrée `**END-PAR**` laisse une ligne blanche ; le Pip-Boy tourne la page après 35 entrées.
La zone de texte fait **374 px** de large (`PIPBOY_WINDOW_CONTENT_VIEW_WIDTH` de fallout2-ce) et est dessinée avec
`font1.aaf` : la limite est en pixels, pas en caractères (environ 53 à 59 selon le texte).

Le panneau « Holodisque du Pip-Boy » (onglet à droite) s'active quand on sélectionne une ligne d'un corps de holodisque
dans `game/pipboy.msg` :

* la **source** s'affiche en paragraphes (les lignes coupées par la mise en page sont recollées, les autres sauts de ligne
  — poèmes, listes — sont conservés) ;
* la **traduction** s'écrit librement dans une zone de texte : une ligne vide commence un paragraphe, un simple retour à la
  ligne force une coupure ;
* le texte est découpé automatiquement à la largeur du Pip-Boy avec la vraie police du jeu (`font1.aaf`, lue dans
  `text/<langue>/` ou à la racine des sources) et écrit dans les enregistrements `id, id+1…` avec les `**END-PAR**` entre
  paragraphes et `**END-DISK**` à la fin. Si la police est introuvable (ou l'encodage multi-octets), la coupure se fait à
  **53 caractères** par ligne (réglable, comme la largeur en pixels) ;
* l'aperçu « Lignes écrites dans le fichier » montre chaque entrée avec son id et sa largeur ; une ligne trop large est
  signalée ;
* si la traduction est plus longue que la source, des enregistrements sont ajoutés après le corps (refusé s'ils entrent
  en collision avec un autre texte) ; si elle est plus courte, les enregistrements en trop gardent leur texte et comptent
  comme faits.

Vérifié sur les vrais `pipboy.msg` : les 21 holodisques de Fallout 2 (RPU) et les 18 de l'installation Fallout 1 se
reconstituent à l'identique (aux espaces près) avec cette largeur et cette police.

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
* `babylon_net_tests` : clients DeepL, Grammalecte et LanguageTool contre un faux serveur HTTP local ; avec
  `BABYLON_GRAMMALECTE_URL`, `BABYLON_LT_URL` ou `BABYLON_DEEPL_KEY` (variable d'environnement uniquement) ils
  essaient aussi les vrais services.
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

Les chaînes sources sont en anglais ; les 9 autres langues sont complètes (278 chaînes). Pour mettre à jour :
`lupdate src -ts i18n/*.ts -no-obsolete`, ajouter les nouvelles chaînes à `tools/translations/fr.py` (dictionnaire) puis,
après `python tools/make_keys.py`, aux listes `VALUES` des autres langues (alignées sur `_keys.py` ; ajouter à la fin) et
lancer `python tools/fill_ts.py`, qui vérifie aussi que les jetons (`%1`, balises HTML, retours à la ligne) sont conservés.
Le menu Affichage > Langue ne propose que les langues dont la traduction est présente.

## Limites connues et suites prévues

* **Exécutable unique** : le build actuel utilise les DLL de Qt (dossier `dist\FalloutBabylon`, ~29 Mo). Un `.exe` unique
  demande de compiler Qt en statique (non fourni par les installeurs officiels) : à faire une fois le périmètre stabilisé.
* Validé sur de vraies données : DAT2 (master.dat / critter.dat de Fallout 2 RPU) et DAT1 (MASTER.DAT de Fallout 1 :
  19 784 entrées dont 14 997 compressées en LZSS, toutes relues à la bonne taille), projets Fallout 1 et Fallout 2 complets,
  952 scripts Fallout 1 passés à `int2ssl` (936 décompilés, 271 avec dialogue ; les 16 autres échouent dans `int2ssl`
  lui-même : « Conditional expression left in stack »).
* « Navigation virtuelle dans les archives » : la vue Projets présente les fichiers `.msg` des sources fusionnées ;
  un navigateur d'archive complet (tous types de fichiers) n'est pas encore fait.
* Le glossaire n'a pas encore d'entrées saisies à la main (il est dérivé des fichiers `/game`).
* Les fichiers `.msg` multi-lignes : le moteur ignore les retours à la ligne à l'intérieur d'un champ ; ils sont ici conservés
  tels quels dans le texte.
