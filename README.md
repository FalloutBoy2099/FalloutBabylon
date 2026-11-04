# Fallout Babylon

Éditeur de traduction pour les fichiers `.msg` de Fallout 1 et Fallout 2 (dialogues, textes du jeu, mods) :
multi-projets, archives DAT1/DAT2 en lecture et en écriture, glossaire, statistiques, relecture, DeepL,
Grammalecte / LanguageTool, mémoire de traduction, arbre de dialogue, holodisques Pip-Boy, interface en 10 langues.

L'application est écrite en **Rust** avec l'interface **Slint** : tout est dans [`rust/`](rust/README.md)
(compilation, organisation, tests).

```bash
cd rust
cargo build --release     # target/release/FalloutBabylon.exe
```

Dossiers :

| Dossier | Contenu |
|---|---|
| `rust/` | Le code (cœur `babylon-core`, application `babylon-app`) |
| `i18n/` | Traductions de l'interface (`.ts`), source de `rust/babylon-app/lang` (voir `rust/tools/gen_translations.py`) |
| `tools/` | `int2ssl.exe`, utilisé pour décompiler les scripts de dialogue |
| `_icons/` | Illustrations d'origine de l'icône |
