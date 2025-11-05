## 🧩 Der ROM-Code (1-Wire Unique ID)

Jeder 1-Wire-Baustein hat einen **weltweit eindeutigen 64-Bit-ROM-Code**, der aus drei logischen Teilen besteht:

| Feld              | Länge           | Beschreibung                                        |
| ----------------- | --------------- | --------------------------------------------------- |
| **Family Code**   | 8 Bit (1 Byte)  | Identifiziert den Chip-Typ (z. B. DS2506 → 0x0F)    |
| **Serial Number** | 48 Bit (6 Byte) | Individuelle Seriennummer, einzigartig pro Baustein |
| **CRC8**          | 8 Bit (1 Byte)  | Prüfsumme über die vorigen 7 Bytes                  |

Gesamt: **8 Bytes = 64 Bit**.

------

## 🔍 Aufbau im Speicher (wie vom `read rom` oder `search rom` Befehl geliefert)

| Byte | Bezeichnung      | Bedeutung                           |
| ---- | ---------------- | ----------------------------------- |
| 0    | **Family Code**  | Identifiziert den Gerätetyp         |
| 1–6  | **Seriennummer** | Fortlaufend oder zufällig generiert |
| 7    | **CRC8**         | Fehlerprüfung, Polynom 0x8C         |

Beispiel aus deinem Dump:

```
ROM: 8B 52 EB 00 00 70 5E B9

```



| Feld         | Wert              | Bedeutung                                         |
| ------------ | ----------------- | ------------------------------------------------- |
| Family Code  | 0x8B              | Normalerweise DS2433, hier jedoch „Fake-ID“       |
| Seriennummer | 52 EB 00 00 70 5E | Einzigartige Gerätekennung                        |
| CRC8         | 0xB9              | Prüfsumme, ergibt mit Polynom 0x8C korrekt 0xB9 ✅ |

## ⚙️ Wie die CRC funktioniert

Die CRC8 wird über die ersten 7 Bytes (Family + Serial) berechnet
 mit dem **Dallas/Maxim-Polynom**:

> x⁸ + x⁵ + x⁴ + 1  →  hex: 0x8C

Das garantiert, dass jedes Bit-Fehlerereignis im ROM-Code erkannt wird.

In C-Code sieht das so aus:

```
uint8_t crc8_dallas(const uint8_t *data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 0x01) ? (crc >> 1) ^ 0x8C : (crc >> 1);
  }
  return crc;
}
```

Wenn also `crc8_dallas(romCode, 7) == romCode[7]`, ist der ROM-Code gültig.

## 🏷️ Bedeutung für dich (DS2506-Clone)

- Dein Baustein meldet **Family Code 0x8B (DS2433)**,
   verhält sich aber **funktionsmäßig wie DS2506 (64 Kbit)**.
- D. h. Software, die nur den Family-Code prüft, denkt, es sei ein DS2433 –
   aber die Speichergröße und Befehle verraten den echten Funktionsumfang.
- Der ROM-Code ist außerdem **read-only** – er wird im Silizium programmiert,
   und kann **nicht verändert oder überschrieben** werden.

------

## 🔒 Typische Verwendung

In Automaten, Geldspielern, Verbrauchsmaterial-Chips usw. dient der ROM-Code als:

- **Seriennummer** oder eindeutige Geräte-ID
- **Integritäts- oder Lizenznachweis** (z. B. bei Bally Wulff)
- **Verknüpfung** zwischen Inhalt des EEPROMs und Hardware-Identität

Bei Bally-Chips z. B. wird der ROM-Code im Spiel-EPROM gespeichert,
 und beim Start abgeglichen – so wird verhindert, dass man beliebige EEPROMs einsetzt.