# -*- coding: utf-8 -*-
"""English sources of the interface, in the order of the aligned VALUES lists of the other languages."""
KEYS = [
    'No DeepL API key. Enter it in Tools > Settings.',  # 0
    'DeepL rejected the API key.',  # 1
    'The DeepL quota is exhausted.',  # 2
    'DeepL error %1',  # 3
    'DeepL does not support %1.',  # 4
    'No glossary can be created for this language pair.',  # 5
    'Open a dialogue file to see its tree.',  # 6
    'The dialogue tree is available for files of the /dialog folder.',  # 7
    'int2ssl was not found next to the application.',  # 8
    'Decompilation failed: %1',  # 9
    'No script found for this dialogue.',  # 10
    'This script contains no dialogue structure.',  # 11
    '%1 nodes · %2 line(s) of the file are not part of the tree',  # 12
    '⤴ %1 (see above)',  # 13
    'Click to jump to the first occurrence',  # 14
    '(line %1 not found in this file)',  # 15
    'NPC',  # 16
    'NPC (no answer)',  # 17
    ' (low IQ)',  # 18
    ' (IQ %1+)',  # 19
    '⚠ Several different translations exist in the game files.',  # 20
    'Not translated yet: translate the matching /game file first.',  # 21
    'Term',  # 22
    'Translation',  # 23
    'Type',  # 24
    'Origin',  # 25
    'Search the glossary...',  # 26
    'Building the glossary...',  # 27
    'No term yet. The glossary is built from the /game message files.',  # 28
    '%1 terms · %2 without translation · %3 inconsistent',  # 29
    'Insert "%1"',  # 30
    'Copy the term',  # 31
    'Copy the translation',  # 32
    'Server error %1',  # 33
    'LanguageTool does not support %1.',  # 34
    '<h2>Fallout Babylon</h2><p>Translation editor for Fallout 1 and Fallout 2 mods.</p>',  # 35
    'Open a game folder...',  # 36
    'New project...',  # 37
    'Projects',  # 38
    'Glossary',  # 39
    'Statistics',  # 40
    'To review',  # 41
    'Dialogue tree',  # 42
    '&Save',  # 43
    'Save &all',  # 44
    '&Close file',  # 45
    'Mark as &to review',  # 46
    'Copy the &source',  # 47
    '&Accept as is',  # 48
    '&Next line',  # 49
    '&Previous line',  # 50
    'Next &untranslated line',  # 51
    'Previous untranslated line',  # 52
    '&Find in this file',  # 53
    '&Translate this line with DeepL',  # 54
    '&Mass translation with DeepL...',  # 55
    '&Settings...',  # 56
    'Project &settings...',  # 57
    '&Export to a DAT archive...',  # 58
    'Refresh &statistics',  # 59
    'Rebuild the &glossary',  # 60
    'Close the pro&ject',  # 61
    '&File',  # 62
    '&New project...',  # 63
    'Open a game &folder...',  # 64
    'Recent &folders',  # 65
    '&Saved projects',  # 66
    '(none)',  # 67
    '&Quit',  # 68
    '&Edit',  # 69
    '&Tools',  # 70
    '&View',  # 71
    '&Theme',  # 72
    'System',  # 73
    'Light',  # 74
    'Dark',  # 75
    '&Language',  # 76
    'Automatic (system)',  # 77
    'Language',  # 78
    'The language will change the next time the application starts.',  # 79
    '&Help',  # 80
    '&About...',  # 81
    'About &Qt...',  # 82
    'Open a Fallout game or mod folder',  # 83
    'Project',  # 84
    'Some sources could not be read:\n\n%1',  # 85
    'No .msg file found under text/%1 or text/%2 in the sources of "%3".',  # 86
    'Project settings',  # 87
    'Save or close the modified files of this project first.',  # 88
    'Analysing "%1"...',  # 89
    'Analysing "%1": %2 / %3 files',  # 90
    'Open',  # 91
    'Cannot read %1:\n%2',  # 92
    '%1 does not exist in the sources of this project.',  # 93
    '%1 contains malformed records; everything after the first one is ignored by the game:\n\n%2',  # 94
    'Unsaved changes',  # 95
    'These files have unsaved changes:\n\n%1',  # 96
    'Save',  # 97
    'Discard',  # 98
    'Export',  # 99
    'Nothing to export yet: save some translated files first.\n(%1 does not exist)',  # 100
    'Fallout 2 archive (zlib) (*.dat)',  # 101
    'Fallout 1 archive (*.dat)',  # 102
    'Export to a DAT archive',  # 103
    '%1 files written to\n%2',  # 104
    'Cannot write the archive:\n%1',  # 105
    '%1 → %2  ·  encoding %3 / %4',  # 106
    'About Fallout Babylon',  # 107
    '<h3>Fallout Babylon %1</h3><p>Translation editor for the dialogue and text files of Fallout 1 and Fallout 2 mods.</p><p>Built with Qt %2 and Scintilla.</p>',  # 108
    'Mass translation with DeepL',  # 109
    'Untranslated lines (%1)',  # 110
    'Selected lines (%1)',  # 111
    'All lines (%1)',  # 112
    'Replace existing translations',  # 113
    'Apply',  # 114
    'ID',  # 115
    'Source',  # 116
    'DeepL proposal (editable)',  # 117
    'Note',  # 118
    'Translate',  # 119
    'Stop',  # 120
    'Apply the checked proposals',  # 121
    'Close',  # 122
    'No DeepL API key: enter it in Tools > Settings.',  # 123
    '%1 line(s), about %2 characters to send to DeepL.',  # 124
    ' Remaining quota: %L1 characters.',  # 125
    'Not enough quota for everything.',  # 126
    'Nothing to translate with this choice.',  # 127
    'DeepL',  # 128
    'Send %1 line(s) (about %2 characters) to DeepL?',  # 129
    'Preparing the glossary...',  # 130
    'Translating...',  # 131
    'Stopped: %1 of %2 line(s) translated, %3 failed.',  # 132
    'Done: %1 line(s) translated, %2 failed. Check the proposals, then apply.',  # 133
    'Stopping...',  # 134
    '%1 translation(s) applied. Save the file to keep them.',  # 135
    'Source (%1)',  # 136
    'Translation (%1)',  # 137
    'St.',  # 138
    'New translation project',  # 139
    'e.g. RPU — French',  # 140
    'Lowest priority first: a later source overrides an earlier one, like in the game.',  # 141
    'Game folder...',  # 142
    'Adds a Fallout folder: its DAT archives and its data folder are detected.',  # 143
    'Mod folder...',  # 144
    'DAT archive...',  # 145
    'Recent',  # 146
    'Up',  # 147
    'Down',  # 148
    'Remove',  # 149
    'Browse...',  # 150
    'Name',  # 151
    'Sources',  # 152
    'Source language',  # 153
    'Source encoding',  # 154
    'Target language',  # 155
    'Target encoding',  # 156
    'Output folder',  # 157
    'Fallout folder',  # 158
    'Mod folder (containing text/ or data/)',  # 159
    'DAT archives',  # 160
    'DAT archives (*.dat);;All files (*)',  # 161
    'Add the game folder or the archives of the mod to translate.',  # 162
    'No text/<language>/ folder found in these sources.',  # 163
    'Languages found: %1.',  # 164
    'Give the project a name.',  # 165
    'Add at least one source.',  # 166
    'Choose the source and the target language.',  # 167
    'The source and target languages must differ.',  # 168
    'Choose an output folder.',  # 169
    'Filter files...',  # 170
    'All files',  # 171
    'Not translated',  # 172
    'Partially translated',  # 173
    'Complete',  # 174
    'File',  # 175
    'Progress',  # 176
    'Lines',  # 177
    'Words left',  # 178
    '1 · Game files — do first',  # 179
    '2 · Dialogues',  # 180
    '3 · Other files',  # 181
    'Recommended: translate the /game files before the /dialog files. They hold the names of items, creatures, skills and places, which feed the glossary.',  # 182
    '%1\n%2 lines, %3 untranslated, %4 to review\n%5 words / %6 characters left',  # 183
    'Refresh statistics',  # 184
    'Rebuild glossary',  # 185
    'Export to a DAT archive...',  # 186
    'Open the output folder',  # 187
    'Project settings...',  # 188
    'Close project',  # 189
    'Empty answer from DeepL.',  # 190
    'A placeholder (@...@, %s...) was altered: check this line.',  # 191
    'DeepL refused the glossary: the free plan allows a single glossary (delete the existing one in your DeepL account to use the project glossary).',  # 192
    'Unknown word: %1',  # 193
    '✔ Key accepted. Usage: %L1 / %L2 characters this period.',  # 194
    '✔ The server answers (%1 issue(s) found in the test sentence).',  # 195
    'Undo',  # 196
    'Redo',  # 197
    'Cut',  # 198
    'Copy',  # 199
    'Paste',  # 200
    'Select all',  # 201
    'Settings',  # 202
    'DeepL API key (free keys end with :fx)',  # 203
    'Test',  # 204
    'Default',  # 205
    'More formal',  # 206
    'Less formal',  # 207
    'Use the project glossary (terms of the /game files; the free plan allows one glossary)',  # 208
    'Send the neighbouring lines as context (not translated, not billed)',  # 209
    'Mark machine translations as "to review"',  # 210
    'API key',  # 211
    'Formality',  # 212
    'Underline spelling and grammar errors in the translation',  # 213
    'Correction',  # 214
    'Grammalecte server (French)',  # 215
    'LanguageTool server (other languages)',  # 216
    'The text of the line being edited is sent to these servers: use servers you trust.',  # 217
    'Contacting DeepL...',  # 218
    'Enter the server address first.',  # 219
    'Contacting the server...',  # 220
    'Done',  # 221
    'Chars left',  # 222
    'Game files',  # 223
    'Dialogues',  # 224
    'Other',  # 225
    'Total',  # 226
    'Only unfinished files',  # 227
    'File / character',  # 228
    'Lines left',  # 229
    '<b>%1</b> — %2 done<br>%3 lines (%4 untranslated, %5 to review)<br>%6 words and %7 characters left (of %8 words)',  # 230
    'No project open.',  # 231
    '<b>%1</b>',  # 232
    '<b>%1</b> — computing...',  # 233
    'Untranslated',  # 234
    'Translated',  # 235
    'Nothing to review. Mark lines with Ctrl+R.',  # 236
    '%1 line(s) to review — double-click to open',  # 237
    'Search in this file (text or ID)...',  # 238
    'All lines',  # 239
    'Glossary alerts',  # 240
    '"%1": not translated in the game files yet',  # 241
    'Insert "%1" in the translation',  # 242
    'Replace with "%1"',  # 243
    'Translate this line with DeepL',  # 244
    'Glossary: %1 → %2',  # 245
    '%1 lines',  # 246
    'Enter your DeepL API key in Tools > Settings first.',  # 247
    'Translating with DeepL...',  # 248
    'Glossary:',  # 249
    '<i>Not translated in the game files yet</i>',  # 250
    'Inconsistent translations in the game files',  # 251
    '%1 / %2 lines',  # 252
    'Cannot save %1:\n%2',  # 253
    "Some characters cannot be represented in the encoding of the target files (%1) and were replaced by '?':\n\n%2\n\nChoose another encoding in the project settings or reword the text.",  # 254
    'DeepL glossary not used: %1',  # 255
    'DeepL glossary',  # 256
    'Your DeepL account already contains a glossary:\n\n%1\n\nThe free plan allows a single glossary. Delete it and replace it with the glossary of this project?\n\nThis cannot be undone.',  # 257
    'Pip-Boy holodisk',  # 258
    ' px',  # 259
    'Width of the Pip-Boy text area (374 px in the game).',  # 260
    'Characters per line, used when the game font cannot be read.',  # 261
    'Reload from the lines',  # 262
    'Rebuild the text of the translation from the records of the file.',  # 263
    'Width:',  # 264
    'Characters (fallback):',  # 265
    'Source (paragraphs)',  # 266
    'Translation (write freely: a blank line starts a paragraph)',  # 267
    'Lines written to the file',  # 268
    'Open game/pipboy.msg and select a holodisk line.',  # 269
    'The holodisk editor works on game/pipboy.msg.',  # 270
    'This line is not part of a holodisk body (a holodisk body ends with **END-DISK**).',  # 271
    'Holodisk: records %1 to %2 (%3 entries, %4 page(s) in the Pip-Boy).',  # 272
    'Measured with the game font (font1.aaf): lines are cut at %1 px.',  # 273
    'Game font not available (or multi-byte encoding): lines are cut at about %1 characters.',  # 274
    'Editing...',  # 275
    '%1 entries, %2 page(s) in the Pip-Boy',  # 276
    '%1 line(s) wider than the screen',  # 277
    'the game only reads 500 entries per holodisk',  # 278
    'The translation needs %1 entries but only %2 ids are free before record %3. Shorten it.',  # 279
    'written to the file',  # 280
    'Translated files are written to %1 (this folder overrides the sources).',  # 281
    'Do not translate: this term must stay unchanged.',  # 282
    'Add...',  # 283
    'Edit...',  # 284
    'Delete',  # 285
    'Import...',  # 286
    'Export...',  # 287
    'Add a term of your own (a name to keep, a word with a fixed translation...)',  # 288
    'Import terms from a CSV / TSV file (term; translation; keep)',  # 289
    'Export your own terms to a CSV file',  # 290
    'New glossary term',  # 291
    'Edit the glossary term',  # 292
    'Do not translate (keep the term as is)',  # 293
    'Term (source language):',  # 294
    'Translation:',  # 295
    'Both the term and its translation are required.',  # 296
    'Select a term first.',  # 297
    'Only the terms you added (in bold) can be deleted.',  # 298
    'Import glossary terms',  # 299
    'Glossaries (*.csv *.tsv *.txt);;All files (*)',  # 300
    'Cannot read %1.',  # 301
    'No term found in this file.',  # 302
    '%1 term(s) imported, %2 line(s) ignored.',  # 303
    'There is no term of your own to export yet.',  # 304
    'Export glossary terms',  # 305
    'CSV files (*.csv)',  # 306
    'Cannot write %1.',  # 307
    'Edit this term...',  # 308
    'Override with my own translation...',  # 309
    'Keep this term as is (do not translate)',  # 310
    'Delete this term',  # 311
    'Import an existing translation',  # 312
    'Reuse the translation of another version of the mod (for instance an older installation translated into %1). Only untranslated lines are filled in unless you ask to replace.',  # 313
    'Game or mod folder holding the translation',  # 314
    'Also reuse translations of identical texts found under another id',  # 315
    'When the source text changed, import the old translation marked "to review"',  # 316
    'Replace translations the project already has',  # 317
    'Import',  # 318
    'Folder',  # 319
    'Language of its source texts',  # 320
    'Language of its translation',  # 321
    'Folder holding the translation',  # 322
    'No text/<language>/ folder found here.',  # 323
    '%1 source(s) found; languages: %2.',  # 324
    'Replace the translations the project already has? This rewrites the files of the output folder.',  # 325
    'Cancel',  # 326
    'Importing...',  # 327
    '%1 file(s) examined, %2 written.\n%3 line(s) with the same text, %4 with a changed source (to review), %5 found under another id; %6 already translated and kept.',  # 328
    'Cancelling...',  # 329
    'Search and replace',  # 330
    'Quality report',  # 331
    '&Validate (or remove the validation)',  # 332
    'Undo the last operation',  # 333
    'Redo the operation',  # 334
    'Search and replace in the &project...',  # 335
    '&Quality report...',  # 336
    '&Import an existing translation...',  # 337
    'The files of this project that are open will be closed first. Continue?',  # 338
    'Replace',  # 339
    'Replace "%1" by "%2" in the translation of %3 line(s) of %4 file(s)?\n\nFiles that are not open are saved immediately.',  # 340
    '%1 replacement(s) in %2 file(s).',  # 341
    'Undo the operation: %1',  # 342
    'Redo the operation: %1',  # 343
    'Changed since it was validated',  # 344
    'Validated',  # 345
    'Comment: %1',  # 346
    'Previous translation: %1',  # 347
    'Select a quest line to see how the Pip-Boy wraps it.',  # 348
    'Quest %1: the Pip-Boy wraps the text (after the number) at %2 px.',  # 349
    'Measured with the game font (font1.aaf).',  # 350
    'Game font not available (or multi-byte encoding): approximate measure.',  # 351
    '%1 line(s) in the translation, %2 in the source',  # 352
    'Custom',  # 353
    'Tokens',  # 354
    'Spacing',  # 355
    'Encoding',  # 356
    'Length',  # 357
    'Inconsistent duplicate',  # 358
    'missing: %1',  # 359
    'unexpected: %1',  # 360
    'the translation only contains spaces',  # 361
    'spaces at the start or the end',  # 362
    'the source has spaces at the start or the end',  # 363
    'doubled spaces',  # 364
    'cannot be written as %1: %2',  # 365
    '%1 characters (limit %2)',  # 366
    'short label of the interface: %1 characters for %2 in the source',  # 367
    '%1 characters for %2 in the source',  # 368
    'elsewhere (%1 times): %2',  # 369
    'Lost tokens (@Name@, %d, {field})',  # 370
    'Characters the encoding cannot hold',  # 371
    'Too long',  # 372
    'Inconsistent duplicates',  # 373
    'no limit',  # 374
    'Maximum number of characters of a translation (0: only report translations much longer than the source)',  # 375
    'Maximum length:',  # 376
    'Run the checks',  # 377
    'The checks apply to the saved files: save your changes first.',  # 378
    'Export (CSV)...',  # 379
    'All problems',  # 380
    'Check',  # 381
    'Problem',  # 382
    'Press "Run the checks".',  # 383
    'Select at least one check.',  # 384
    'Checking...',  # 385
    'Checking... %1 / %2 files',  # 386
    'No problem found.',  # 387
    '%1 problem(s)',  # 388
    ' — %1 shown',  # 389
    'only the first %1 are listed (use the export for all)',  # 390
    'Export the report',  # 391
    'Search in the whole project...',  # 392
    'Replace with (in the translation)...',  # 393
    'In the translation',  # 394
    'In the source',  # 395
    'In both',  # 396
    'Regular expression',  # 397
    'Match case',  # 398
    'Whole words',  # 399
    'Search',  # 400
    'Replace the checked lines',  # 401
    'Replace all',  # 402
    'Text',  # 403
    'After replacement',  # 404
    'Type a text and press Enter.',  # 405
    'Type a text to search.',  # 406
    'Invalid regular expression: %1',  # 407
    'Searching...',  # 408
    ' (source)',  # 409
    'No result.',  # 410
    '%1 line(s), %2 occurrence(s)',  # 411
    'only the first results are shown',  # 412
    'Not validated',  # 413
    'Changed since validation',  # 414
    'With a comment',  # 415
    'Comment on this line (kept in the project, not in the game files)...',  # 416
    'Restore',  # 417
    'Put the previous translation back',  # 418
    'Previous translation restored',  # 419
    'DeepL translation',  # 420
    'Mass translation',  # 421
    'Review mark',  # 422
    'Validation',  # 423
    'Copy the source',  # 424
    'Accept as is',  # 425
    '&Update the output DAT archive now',  # 426
    'Updating the DAT archive...',  # 427
    'DAT archive updated: %1 translated file(s), %2 original(s).',  # 428
    'DAT archive NOT updated: %1',  # 429
    'DAT archive',  # 430
    'The DAT archive could not be written:\n%1',  # 431
    'The output archive cannot be one of the sources of the project.',  # 432
    'The output archive must be a .dat file.',  # 433
    'Folder of loose files',  # 434
    'e.g. french.dat',  # 435
    'Fallout 2 (zlib)',  # 436
    'Fallout 1',  # 437
    'Also store the original texts (reference files)',  # 438
    'Adds text/<source language>/ copies of the translated files, so that the reference of each translation is in the archive. Turn it off if a later mod must keep providing the original texts.',  # 439
    'Format:',  # 440
    'Output',  # 441
    'Output archive',  # 442
    'DAT archives (*.dat)',  # 443
    'The translation is delivered in %1, rebuilt after each save. It contains text/%2/ with the translated files',  # 444
    ', and the original texts under text/%1/.',  # 445
    'Choose the output archive.',  # 446
]
