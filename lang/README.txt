D2R VR Authentic Settings - its languages
==========================================

Each file here is one language of the settings program (D2R_VR_Settings.exe, beside the game's D2R.exe):
en.txt is the English built into the program, the others are its translations. Pick one in Settings > Home >
Language; the program opens again in it. "Windows' language" takes the file for your Windows language when
there is one.

The translations that come with the mod were made by machine and checked only in part - corrections are
welcome: post them in our Discord (#d2r-vr-feedback), or send the corrected file.

Your own language
-----------------
1. Copy en.txt to <your language's code>.txt in this folder - pl.txt, it.txt, ja.txt, tr.txt, pt-PT.txt...
2. Change the line "language.name = English" to your language's own name ("language.name = Polski").
3. Translate the text after each "=" - by hand, or give the file to an AI or a translator and ask it to keep
   every line's key. Keep as they are:
     - the key before the "=" on each line;
     - every {name} in braces - the program puts a number or a name there; it may move within the sentence;
     - ini names such as [hud] bar_place or d2r_vr.ini, file and folder names, units (cm, m/s, Hz, %);
     - \n - a new line inside a text.
   Lines starting with # are comments. A line you leave out, or leave empty, stays English - so you can
   translate a part and add the rest later.
4. Save as UTF-8 (Notepad's default), open Settings, and pick your language in Home > Language.

After an update of the mod, en.txt may have new lines: copy them into your file. A file of your own here is
never overwritten by the mod's Setup (only the files that come with the mod are) - but uninstalling the mod
removes this folder with the rest of the mod: keep a copy of your file.

For the mod's developers: tools/lang_sync.py makes en.txt from the program and lists, for each file, the lines
missing or made from an English that has changed since (python tools/lang_sync.py check).
