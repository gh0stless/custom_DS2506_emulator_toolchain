# custom_DS2506_emulator_toolchain



Kein vollwertiger DS2506 Emulator, emuliert nur die 8 Speicherseiten die wirklich von dieser Anwendung genutzt werden.



Läuft gut auf Arduino UNO oder Nano.

Am Reader sowie am Emulator ist der Datenpin = 4. Am Emulator Pin 2 Taster gegen Masse zum Rücksetzen (Flashinhalt neu in EPROM).



Der Reader arbeitet mit einem Pythonscript zusammen.

python -m pip install --upgrade pip
python -m pip install pyserial

python read_ds2506.py comX:

Das Python-Script kann automatisch eine ds2506_image.h erzeugen. Diese in den Arduino Projekt Ordner des emulators kopieren und kompilieren.

Der ROM Code muss noch mit Hand eingetragen werden in die .ino



Für Betrieb am Gerät muss ein Levelshifter gebastelt werden weil das Original mit 12V programmiert wird, siehe Anregung Doc.



Die ganze Sache ist von mir nicht getestet an einem Gerät sollte aber funzen, aber jeglicher Hafungsausschluss!



