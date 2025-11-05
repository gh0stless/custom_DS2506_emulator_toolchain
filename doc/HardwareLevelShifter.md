## 1. Hardware-Levelshifter → Library bleibt unverändert

(das ist meist die angenehmere Lösung)

Die OneWireHub-Library geht davon aus, dass es **genau eine 1-Wire-Leitung** gibt, an einem einzigen Arduino-Pin. Dieser Pin wird:

- kurzzeitig als Ausgang LOW geschaltet (Slave zieht Bus runter),
- sonst als Eingang (High-Z) gelesen.

Die Library kümmert sich selbst um die sauberen 1-Wire-Timings und das Umschalten Input/Output. Sie erwartet aber, dass diese Leitung maximal 5 V sieht.

Wenn Sie jetzt einfach den Originalbus (der im Programmierfall auf ca. 12 V hochgeht) direkt an diesen Arduino-Pin hängen, grillt es den AVR.

Die elegante Lösung ist deshalb:
 Sie bauen eine **bidirektionale Pegel-/Schutzstufe**, die aus der 12-V-/Original-DQ-Leitung eine „lokale 5-V-DQ-Leitung“ macht. Die lokale 5-V-DQ-Leitung geht dann 1:1 an den Pin, den OneWireHub kennt. Aus Sicht der Library ist dann alles „wie immer“. Keine Codeänderungen nötig.

So eine Stufe kann man mit einem kleinen N-Kanal-MOSFET (z. B. 2N7002 mit genügend Spannungsfestigkeit) aufbauen, ähnlich wie man es bei I²C-Levelshiftern macht:

- **Drain** des MOSFET → an die Original-DQ (die Seite, die im schlimmsten Fall 12 V bekommt).
- **Source** des MOSFET → an Ihre lokale 5-V-DQ, die direkt zum Arduino-Pin geht.
- **Gate** des MOSFET → an die Source (also an die lokale 5-V-DQ).
- Auf der lokalen Seite ein Pull-up nach +5 V (z. B. 4,7 kΩ).
- Masse der beiden Welten verbinden.

Wirkung:

- Wenn der Master die Leitung runterzieht → über Body-Diode/MOSFET wird Ihre lokale Leitung auch LOW. Arduino sieht sauber „0“.
- Wenn Ihre Emulation die Leitung runterzieht → sie zieht die lokale Seite LOW, der MOSFET leitet durch und zwingt auch die Originalleitung auf LOW. So kann Ihr Emulator antworten.
- Wenn der Master die Programmierspannung hochlegt (12 V) → der MOSFET sperrt in diese Richtung, die 12 V kommen nicht auf die Arduino-Seite. Ihre lokale Seite bleibt einfach „HIGH“ auf 5 V.

Das ist genau das Verhalten, das wir brauchen:

- nach außen nur eine Leitung,
- nach innen trotzdem nur eine Leitung,
- aber galvanisch „entschärft“.





BS138