# Fallout Babylon — version Rust

Éditeur de traduction (fichiers `.msg` de Fallout 1 / 2) en **Rust** avec l'interface **Slint**. Il remplace l'ancienne
version C++/Qt (toujours dans l'historique git) et relit ses fichiers : projets `.fbproj`, état `<id>.state.json`
(commentaires, validations, termes personnalisés, textes de scripts), caches `stats` et `qa`, dossier `<id>.output`,
`<id>.scripts`, dans `%APPDATA%\FalloutBabylon\FalloutBabylon\projects`. Les réglages de l'application (clé DeepL, serveurs de correction, thème, langue…) sont dans
`settings.json` du même dossier (variable `BABYLON_DATA_DIR` pour un autre dossier de données).

## Compiler et lancer

```bash
cd rust
cargo build --release          # produit target/release/FalloutBabylon.exe (un seul exécutable, icône incluse)
cargo run --release
cargo test --release           # 86 tests du cœur
```

Tests sur de vraies données (désactivés par défaut), voir `babylon-core/tests/real_data.rs` :
`BABYLON_SAMPLE_DAT`, `BABYLON_SAMPLE_FOLDER`, `BABYLON_SAMPLE_INSTALL`, `BABYLON_TM_NEW` / `BABYLON_TM_OLD`,
`BABYLON_F1_SCRIPTS`, puis `cargo test --release -p babylon-core --test real_data -- --nocapture`.

## Organisation

| Crate / dossier | Rôle |
|---|---|
| `babylon-core` | Tout ce qui n'est pas interface : DAT1/DAT2 (lecture/écriture), `.msg`, encodages, VFS des sources, glossaire, statistiques, projet et état, DeepL, Grammalecte / LanguageTool, mémoire de traduction, qualité, recherche, archive de sortie, scripts `.int`, graphe de dialogue, texte Pip-Boy… |
| `babylon-app` | Application Slint : `ui/*.slint` (fenêtre, panneaux, dialogues) et `src/app/` (état, éditeur, panneaux, dialogues). |
| `babylon-app/lang` | Traductions générées par `tools/gen_translations.py` à partir des `.ts` du dépôt (`../i18n`) + `tools/extra_translations.py` (textes propres à la version Rust). |
| `tools/` | `gen_translations.py` (régénère `lang/`), `missing_translations.py` (textes sans traduction). |

Les 10 langues de l'interface (en, fr, es, it, pt, de, pl, cs, ru, zh) sont embarquées ; le menu *Affichage > Langue*
change la langue immédiatement (pas de redémarrage).

## Fonctions

Fonctions portées : multi-projets et onglets, tableau filtrable, éditeurs source / traduction avec surlignage du
glossaire et soulignement orthographe / grammaire, relecture, validation, commentaires, annulation par opération,
panneaux (glossaire avec termes personnalisés et CSV, statistiques, à relire, suggestions, recherche / remplacement,
rapport qualité, textes des scripts, arbre de dialogue, holodisques Pip-Boy), DeepL (ligne, traduction de masse,
glossaire du compte), importation d'une traduction existante, rapport de jeu (import et suivi), sortie en dossier ou en
archive DAT (reconstruite après chaque enregistrement), export DAT, thèmes clair / sombre / système.

Différences d'interface assumées : panneaux regroupés dans un seul volet à droite (sélecteur) au lieu de docks
détachables ; les info-bulles sur les termes sont remplacées par une barre d'information sous l'éditeur (cliquer dans un
terme ou une erreur affiche les actions : insérer, remplacer).

## Tests d'interface et captures

`babylon-app/tests/ui.rs` pilote la vraie fenêtre sans écran (saisie, glossaire, sauvegarde, recherche, rapport qualité).
Pour une capture : `BABYLON_SHOT=capture.png` (et `BABYLON_SHOT_DELAY`, `BABYLON_OPEN_PROJECT`, `BABYLON_OPEN_FILE`,
`BABYLON_OPEN_ROW`, `BABYLON_PANEL`, `BABYLON_DIALOG=project|settings|mass`).
