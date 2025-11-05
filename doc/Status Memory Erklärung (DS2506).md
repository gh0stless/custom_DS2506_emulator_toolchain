## Status Memory Erklärung (DS2506)

Der DS2506 hat **256 Bytes Status Memory** - das ist wie ein "Kontrollbereich" mit Infos über den Speicher:

### **Aufbau (256 Bytes total):**

0x000 - 0x01F  (32 Bytes)  = Write-Protect Bits
0x020 - 0x03F  (32 Bytes)  = EPROM Mode Bits
0x040 - 0x05F  (32 Bytes)  = Used Pages (TMEX)
0x060 - 0x0FF  (160 Bytes) = Reserved/Manufacturer
0x100 - 0x11F  (32 Bytes)  = Copy-Protect Bits
0x120 - 0x1FF  (224 Bytes) = Reserved
```
Die wichtigsten Bereiche:

1. Write-Protect (0x000-0x01F)**
Was es ist: Jedes Bit = 1 Page (32 Bytes)
- Bit = 1 (0xFF): Page ist beschreibbar
- Bit = 0: Page ist geschützt (nicht mehr schreibbar)
Beispiel:
0x000: FF FF FF FF → Erste 32 Pages beschreibbar
0x000: FE FF FF FF → Page 0 geschützt!
Wofür? Verhindert versehentliches Überschreiben wichtiger Daten (z.B. Seriennummern)

2. EPROM Mode (0x020-0x03F)
Was es ist: Zeigt ob Page schon programmiert wurde
- 0xFF: Page ist leer (noch nie beschrieben)
- 0x00 oder andere: Page wurde programmiert
Wofür? EPROM kann nur einmal beschrieben werden! Diese Bits zeigen welche Pages schon "verbraucht" sind.

3. Copy-Protect (0x100-0x11F)
Was es ist: Verhindert Kopieren der Daten
- Bit = 1: Page ist kopierbar
- Bit = 0: Page ist kopiergeschützt
Wofür? Schutz vor Klonen/Duplikaten (z.B. bei Lizenzschlüsseln, Spiel-Dongles)

4. Used Pages (0x040-0x05F) - TMEX spezifisch
Was es ist: Bitmap welche Pages belegt sind
- Bit = 1: Page ist leer
- Bit = 0: Page ist benutzt

Wofür? TMEX Dateisystem kann so schnell freie Pages finden
```


```
Praktisches Beispiel:

Status Memory Bytes:
0x000: FF FF FF FE  → Pages 0-23 beschreibbar, Page 24 geschützt
0x020: 00 00 FF FF  → Pages 0-15 programmiert, Rest leer
0x100: FF FF FF FF  → Alle Pages kopierbar
```



**Bedeutung:**

- Die ersten 16 Pages sind bereits mit Daten beschrieben
- Page 24 ist gegen Überschreiben geschützt
- Alle Daten können kopiert werden

------

## Bei deinem Chip (Fake DS2433 mit 8KB):

Dein Status Memory zeigt vermutlich:

- **Welche Pages** deine Spielautomaten-Daten enthalten
- **Welche geschützt** sind (wichtige Config/Seriennummer)
- **Ob der Chip klonbar** ist (Copy-Protect)

**Tipp:** Schau dir die ersten 32 Bytes (0x000-0x01F) an - da siehst du welche Pages wichtig/geschützt sind! 🔒