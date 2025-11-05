// =====================================================
//  DS2506 Emulator Write/Read Test (Arduino UNO / OneWire)
//  OneWire an D4 (4k7 Pull-Up). Für EMULATOR gedacht.
//  Ein echter DS2506 (EPROM) braucht 12V -> hier NICHT beschreiben!
// =====================================================

#include <OneWire.h>

static const uint8_t OW_PIN = 4;
OneWire ds(OW_PIN);

// --- ROM / Suche ---
uint8_t rom[8];
bool haveRom = false;

// ---------- Helpers ----------
void printHexByte(uint8_t b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}
void printRom(const uint8_t *r) {
  for (int i = 0; i < 8; i++) { printHexByte(r[i]); if (i < 7) Serial.print(' '); }
}
bool findFirstDevice() {
  ds.reset_search();
  if (ds.search(rom)) { haveRom = true; return true; }
  haveRom = false; return false;
}
bool resetAndSelect() {
  if (!haveRom) return false;
  if (!ds.reset()) return false;
  ds.select(rom);
  return true;
}
bool readLine(Stream &s, String &line) {
  line = s.readStringUntil('\n'); // bis LF oder Timeout
  line.trim();                    // CR/LF/Spaces an Rändern weg
  return line.length() > 0;
}
bool parseNum(const String &tok, uint16_t &out) {
  if (tok.length() == 0) return false;
  const char* p = tok.c_str();
  if (tok.startsWith("0x") || tok.startsWith("0X")) out = (uint16_t) strtoul(p+2, nullptr, 16);
  else                                             out = (uint16_t) strtoul(p,   nullptr, 10);
  return true;
}
void hexdump(uint16_t addr, const uint8_t *buf, uint16_t len) {
  for (uint16_t i = 0; i < len; i++) {
    if ((i % 16) == 0) {
      if (i) Serial.println();
      Serial.print("0x");
      uint16_t a = addr + i;
      if (a < 0x1000) Serial.print('0');
      if (a < 0x0100) Serial.print('0');
      if (a < 0x0010) Serial.print('0');
      Serial.print(a, HEX);
      Serial.print(": ");
    }
    printHexByte(buf[i]); Serial.print(' ');
  }
  Serial.println();
}
void makeTestPattern(uint8_t *buf, uint16_t len, uint8_t seed) {
  for (uint16_t i = 0; i < len; i++) buf[i] = (uint8_t)(seed + i) ^ 0x5A;
}
void computeExpectedOtp(const uint8_t *oldv, const uint8_t *wr, uint8_t *exp, uint16_t n) {
  for (uint16_t i = 0; i < n; i++) exp[i] = oldv[i] & wr[i];
}
bool equalBuf(const uint8_t *a, const uint8_t *b, uint16_t n) {
  for (uint16_t i=0;i<n;i++) if (a[i]!=b[i]) return false;
  return true;
}

// ---------- DS2506 Grundfunktionen ----------

// READ MEMORY (0xF0) – liest nur Nutzdaten (ohne Schluss-CRC)
bool ds2506_readMemory(uint16_t addr, uint8_t *buf, uint16_t len) {
  if (!resetAndSelect()) return false;
  ds.write(0xF0);                  // READ MEMORY
  ds.write(addr & 0xFF);           // TA1
  ds.write((addr >> 8) & 0xFF);    // TA2
  for (uint16_t i = 0; i < len; i++) buf[i] = ds.read();
  return true;
}

// WRITE MEMORY (0x0F) – Emulator-only.
// Liest am Ende 2 Bytes CRC16(dev) (invertiert) aus – optional informativ.
bool ds2506_writeMemory(uint16_t addr, const uint8_t *data, uint16_t len, bool readBackCrc=true) {
  if (!resetAndSelect()) return false;
  ds.write(0x0F);                  // WRITE MEMORY
  ds.write(addr & 0xFF);           // TA1
  ds.write((addr >> 8) & 0xFF);    // TA2
  for (uint16_t i = 0; i < len; i++) {
    ds.write(data[i]);
    delayMicroseconds(30);         // konservative Lücke
  }
  if (readBackCrc) {
    uint8_t crcLo = ds.read();
    uint8_t crcHi = ds.read();
    Serial.print(F("  CRC(dev) = ")); printHexByte(crcLo); Serial.print(' '); printHexByte(crcHi); Serial.println();
  }
  return true;
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  while (!Serial) { ; }

  Serial.println();
  Serial.println(F("=== DS2506 Emulator Write/Read Test ==="));
  Serial.println(F("Hinweis: Nur fuer Emulator. Echter DS2506 braucht 12V zum Programmieren."));

  if (!findFirstDevice()) {
    Serial.println(F("Kein 1-Wire-Geraet gefunden. Bitte anschliessen und resetten."));
  } else {
    Serial.print(F("Gefundenes ROM: ")); printRom(rom); Serial.println();
  }

  Serial.println(F("\nBefehle:"));
  Serial.println(F("  d <addr> <len>    - READ MEMORY Dump"));
  Serial.println(F("  w <addr> <len>    - WRITE MEMORY (OTP, Emulator) mit Testmuster"));
  Serial.println(F("  r                 - Bus neu scannen"));
  Serial.println(F("  rom               - ROM-ID anzeigen"));
  Serial.println(F("Beispiel: d 0x0000 64"));
  Serial.println(F("Beispiel: w 0x0200 32"));
  Serial.println();

  // Optionaler kurzer Selbsttest auf einer gemappten Page (0x0200)
  if (haveRom) {
    const uint16_t baseAddr = 0x0200;     // sicher gemappt
    const uint16_t n = 32;
    uint8_t oldv[n], wr[n], exp[n], rd[n];

    // 1) Vorherzustand lesen
    ds2506_readMemory(baseAddr, oldv, n);
    // 2) Schreibmuster
    makeTestPattern(wr, n, 0xA5);
    computeExpectedOtp(oldv, wr, exp, n);

    Serial.print(F("Schreibe ")); Serial.print(n); Serial.print(F(" Byte an 0x"));
    Serial.print(baseAddr, HEX); Serial.println(F(" ..."));
    bool okW = ds2506_writeMemory(baseAddr, wr, n);
    Serial.println(okW ? F("WRITE ok (Emulator).") : F("WRITE fehlgeschlagen."));

    Serial.print(F("Lese zur Verifikation ... "));
    bool okR = ds2506_readMemory(baseAddr, rd, n);
    Serial.println(okR ? F("READ ok.") : F("READ fehlgeschlagen."));

    if (okW && okR) {
      Serial.println(F("\nSoll-Daten (OTP, expected = old & written):"));
      hexdump(baseAddr, exp, n);
      Serial.println(F("\nIst-Daten:"));
      hexdump(baseAddr, rd, n);
      Serial.println(equalBuf(exp, rd, n) ? F("\nVERIFIKATION: OK ✅")
                                           : F("\nVERIFIKATION: FEHLER ❌"));
      Serial.println();
    }
  }
}

// ---------- Loop / Parser ----------
void loop() {
  if (!haveRom) {
    delay(500);
    if (findFirstDevice()) { Serial.print(F("Jetzt gefundenes ROM: ")); printRom(rom); Serial.println(); }
    return;
  }

  if (!Serial.available()) return;

  String line;
  if (!readLine(Serial, line)) return;

  int sp1 = line.indexOf(' ');
  String cmd = (sp1 < 0) ? line : line.substring(0, sp1);
  cmd.trim();

  String rest = (sp1 < 0) ? "" : line.substring(sp1 + 1);
  rest.trim();

  String tok1, tok2;
  if (rest.length()) {
    int sp2 = rest.indexOf(' ');
    tok1 = (sp2 < 0) ? rest : rest.substring(0, sp2);
    tok1.trim();
    if (sp2 >= 0) { tok2 = rest.substring(sp2 + 1); tok2.trim(); }
  }

  if (cmd.equalsIgnoreCase("d")) {
    uint16_t addr, len;
    if (!parseNum(tok1, addr) || !parseNum(tok2, len)) { Serial.println(F("Syntax: d <addr> <len>")); return; }
    uint16_t n = len;
    static uint8_t buf[128];
    Serial.print(F("READ 0x")); Serial.print(addr, HEX);
    Serial.print(F(", ")); Serial.print(n); Serial.println(F(" Bytes"));
    while (n) {
      uint16_t chunk = n > sizeof(buf) ? sizeof(buf) : n;
      if (!ds2506_readMemory(addr, buf, chunk)) { Serial.println(F("READ fehlgeschlagen.")); break; }
      hexdump(addr, buf, chunk);
      addr += chunk; n -= chunk;
    }
  }
  else if (cmd.equalsIgnoreCase("w")) {
    uint16_t addr, len;
    if (!parseNum(tok1, addr) || !parseNum(tok2, len)) { Serial.println(F("Syntax: w <addr> <len>")); return; }
    if (len > 128) { Serial.println(F("Max 128 Bytes pro Schreibvorgang.")); return; }

    static uint8_t oldv[128], wr[128], exp[128], rd[128];

    // Vorherzustand lesen (für OTP-Erwartung)
    if (!ds2506_readMemory(addr, oldv, len)) { Serial.println(F("READ (alt) fehlgeschlagen.")); return; }

    makeTestPattern(wr, len, (uint8_t)(addr ^ len));
    computeExpectedOtp(oldv, wr, exp, len);

    Serial.print(F("WRITE 0x")); Serial.print(addr, HEX);
    Serial.print(F(", ")); Serial.print(len); Serial.println(F(" Bytes (Emulator)"));
    if (!ds2506_writeMemory(addr, wr, len)) { Serial.println(F("WRITE fehlgeschlagen.")); return; }

    if (!ds2506_readMemory(addr, rd, len)) { Serial.println(F("READ zur Verifikation fehlgeschlagen.")); return; }

    bool ok = equalBuf(exp, rd, len);
    if (!ok) {
      Serial.println(F("Soll (OTP: old & written):"));
      hexdump(addr, exp, len);
      Serial.println(F("\nIst:"));
      hexdump(addr, rd, len);
    }
    Serial.println(ok ? F("VERIFIKATION: OK ✅") : F("VERIFIKATION: FEHLER ❌"));
    if (!ok) {
      Serial.println(F("Hinweis: OTP 1->0 – bereits gebrannte 0-Bits bleiben 0."));
    }
  }
  else if (cmd.equalsIgnoreCase("r")) {
    if (findFirstDevice()) { Serial.print(F("Gefundenes ROM: ")); printRom(rom); Serial.println(); }
    else                   { Serial.println(F("Kein Geraet gefunden.")); }
  }
  else if (cmd.equalsIgnoreCase("rom")) {
    if (haveRom) { printRom(rom); Serial.println(); }
    else         { Serial.println(F("Kein Geraet.")); }
  }
  else if (cmd.length()==0) {
    // ignore
  }
  else {
    Serial.println(F("Unbekannt. Befehle: d <addr> <len> | w <addr> <len> | r | rom"));
  }
}

