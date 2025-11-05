// =====================================================
//  DS2506 EPROM Reader mit OneWire-Serial-Fix
//  Arduino UNO, OneWire-Pin = D4 (mit 4,7k Pull-Up)
// =====================================================

#include <OneWire.h>

OneWire ds(4);
byte romCode[8];

// -----------------------------------------------------
void printHexByte(byte b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}

// -----------------------------------------------------
bool findDevice() {
  ds.reset_search();
  if (!ds.search(romCode)) return false;
  
  if (OneWire::crc8(romCode, 7) != romCode[7]) {
    Serial.println("ROM CRC Fehler!");
    return false;
  }
  return true;
}

// -----------------------------------------------------
void printROM() {
  Serial.print("ROM Code: ");
  for (int i = 0; i < 8; i++) {
    printHexByte(romCode[i]);
    Serial.print(' ');
  }
  Serial.println();
  Serial.print("Family Code: 0x");
  printHexByte(romCode[0]);
  Serial.println();
}

// -----------------------------------------------------
bool readMemory(uint16_t addr, uint16_t len) {
  if (!ds.reset()) {
    Serial.println("Reset fehlgeschlagen!");
    return false;
  }
  
  ds.write(0xCC);
  ds.write(0xF0);
  ds.write(addr & 0xFF);
  ds.write((addr >> 8) & 0xFF);
  
  for (uint16_t i = 0; i < len; i++) {
    if (i % 16 == 0) {
      Serial.print("0x");
      uint16_t currentAddr = addr + i;
      if (currentAddr < 0x1000) Serial.print('0');
      if (currentAddr < 0x0100) Serial.print('0');
      if (currentAddr < 0x0010) Serial.print('0');
      Serial.print(currentAddr, HEX);
      Serial.print(": ");
    }
    
    byte b = ds.read();
    printHexByte(b);
    Serial.print(' ');
    
    if ((i + 1) % 16 == 0) Serial.println();
  }
  
  if (len % 16 != 0) Serial.println();
  return true;
}

// -----------------------------------------------------
void readStatusMemory() {
  Serial.println("\n=== Status Memory (256 Bytes, CRC bereinigt) ===");
  if (!ds.reset()) {
    Serial.println("Kein Reset-Antwort vom Baustein");
    return;
  }

  // SKIP ROM, READ STATUS, Adresse 0x0000
  ds.write(0xCC);
  ds.write(0xAA);
  ds.write(0x00);
  ds.write(0x00);

  uint16_t outIndex = 0;

  for (uint8_t block = 0; block < 32; block++) {
    // 8 echte Status-Bytes
    for (uint8_t i = 0; i < 8; i++) {
      byte b = ds.read();

      // hübsch formatieren wie vorher
      if (outIndex % 16 == 0) {
        Serial.print("0x");
        if (outIndex < 0x10) Serial.print('0');
        Serial.print(outIndex, HEX);
        Serial.print(": ");
      }

      printHexByte(b);
      Serial.print(' ');

      outIndex++;
      if (outIndex % 16 == 0) Serial.println();
    }

    // 2 CRC-Bytes lesen und verwerfen
    byte crcLo = ds.read();
    byte crcHi = ds.read();
    (void)crcLo;
    (void)crcHi;
  }

  if (outIndex % 16 != 0) Serial.println();
  Serial.println();
}


// -----------------------------------------------------
/*void readAllMemory() {
  Serial.println("\n=== KOMPLETTER SPEICHER (8192 Bytes) ===\n");
  unsigned long startTime = millis();
  
  readMemory(0x0000, 8192);
  
  unsigned long duration = millis() - startTime;
  Serial.println("\n========================================");
  Serial.print("Fertig! Dauer: ");
  Serial.print(duration / 1000);
  Serial.print(".");
  Serial.print(duration % 1000);
  Serial.println(" Sekunden");
  Serial.println("========================================");
}*/

void readAllMemory() {
  // Neue Version: Seitenorientierte Ausgabe
  // - Jede Page = 32 Bytes
  // - 256 Pages für 8192 Bytes total
  // - Wir lesen wie bisher direkt per READ MEMORY (0xF0),
  //   aber wir strukturieren die Ausgabe anders.

  const uint16_t TOTAL_SIZE = 8192;
  const uint16_t PAGE_SIZE  = 32;
  const uint16_t BYTES_PER_LINE = 16;

  Serial.println();
  Serial.println("=== PAGE VIEW DES KOMPLETTEN SPEICHERS ===");
  unsigned long startTime = millis();

  // Einmalig den kompletten Speicher sequenziell lesen
  // (das hattest Du sinngemäß ja eh schon gemacht)
  if (!ds.reset()) {
    Serial.println("Reset fehlgeschlagen!");
    return;
  }

  ds.write(0xCC);        // SKIP ROM
  ds.write(0xF0);        // READ MEMORY
  ds.write(0x00);        // Startadresse low
  ds.write(0x00);        // Startadresse high

  // Jetzt 8192 Bytes nacheinander abholen
  for (uint16_t addr = 0; addr < TOTAL_SIZE; addr += PAGE_SIZE) {
    // Kopf für die Page
    uint16_t pageNum = addr / PAGE_SIZE;
    uint16_t rangeStart = addr;
    uint16_t rangeEnd   = addr + PAGE_SIZE - 1;

    // Schön formatiert mit führenden Nullen
    Serial.print("\nPage ");
    if (pageNum < 100) Serial.print('0');
    if (pageNum < 10)  Serial.print('0');
    Serial.print(pageNum);
    Serial.print(" (0x");
    if (rangeStart < 0x1000) Serial.print('0');
    if (rangeStart < 0x0100) Serial.print('0');
    if (rangeStart < 0x0010) Serial.print('0');
    Serial.print(rangeStart, HEX);
    Serial.print(" - 0x");
    if (rangeEnd < 0x1000) Serial.print('0');
    if (rangeEnd < 0x0100) Serial.print('0');
    if (rangeEnd < 0x0010) Serial.print('0');
    Serial.print(rangeEnd, HEX);
    Serial.println("):");

    // Zwei Zeilen à 16 Byte:
    for (uint8_t lineOff = 0; lineOff < PAGE_SIZE; lineOff += BYTES_PER_LINE) {
      uint16_t lineAddr = addr + lineOff;

      // Adress-Label
      Serial.print("  ");
      Serial.print("0x");
      if (lineAddr < 0x1000) Serial.print('0');
      if (lineAddr < 0x0100) Serial.print('0');
      if (lineAddr < 0x0010) Serial.print('0');
      Serial.print(lineAddr, HEX);
      Serial.print(": ");

      // 16 Bytes Hex + ASCII sammeln
      char asciiBuf[BYTES_PER_LINE + 1];
      for (uint8_t i = 0; i < BYTES_PER_LINE; i++) {
        byte b = ds.read();

        // Hex
        if (b < 0x10) Serial.print('0');
        Serial.print(b, HEX);
        Serial.print(' ');

        // ASCII
        if (b >= 32 && b < 127) {
          asciiBuf[i] = (char)b;
        } else {
          asciiBuf[i] = '.';
        }
      }
      asciiBuf[BYTES_PER_LINE] = '\0';

      // ASCII hinten dran
      Serial.print(" ");
      Serial.println(asciiBuf);
    }
  }

  unsigned long duration = millis() - startTime;
  Serial.println("\n========================================");
  Serial.print("Fertig! Dauer: ");
  Serial.print(duration / 1000);
  Serial.print(".");
  Serial.print(duration % 1000);
  Serial.println(" Sekunden");
  Serial.println("========================================");
  Serial.println();
}

void listUsedPages() {
  const uint16_t TOTAL_SIZE   = 8192;   // 8KB
  const uint16_t PAGE_SIZE    = 32;     // 32 Byte pro Page
  const uint16_t PAGE_COUNT   = TOTAL_SIZE / PAGE_SIZE; // 256 Pages

  // Speicher für Page-Auswertung
  bool pageUsed[256];
  for (uint16_t i = 0; i < PAGE_COUNT; i++) {
    pageUsed[i] = false;
  }

  Serial.println();
  Serial.println("=== PAGE BELEGUNGSCHECK ===");

  // 1-Wire Reset
  if (!ds.reset()) {
    Serial.println("Reset fehlgeschlagen!");
    return;
  }

  // READ MEMORY ab Adresse 0x0000 sequenziell
  ds.write(0xCC);        // SKIP ROM
  ds.write(0xF0);        // READ MEMORY
  ds.write(0x00);        // Addr LSB
  ds.write(0x00);        // Addr MSB

  // Wir laufen jetzt einmal komplett durch alle 8192 Bytes,
  // entscheiden pageweise, ob alles FF ist oder nicht.
  for (uint16_t addr = 0; addr < TOTAL_SIZE; addr++) {
    byte b = ds.read();
    if (b != 0xFF) {
      uint16_t pageNum = addr / PAGE_SIZE;
      pageUsed[pageNum] = true;
    }
  }

  // Ausgabe aller belegten Pages
  uint16_t usedCount = 0;
  for (uint16_t page = 0; page < PAGE_COUNT; page++) {
    if (pageUsed[page]) {
      usedCount++;

      uint16_t rangeStart = page * PAGE_SIZE;
      uint16_t rangeEnd   = rangeStart + PAGE_SIZE - 1;

      Serial.print("Page ");
      if (page < 100) Serial.print('0');
      if (page < 10)  Serial.print('0');
      Serial.print(page);

      Serial.print(" (0x");
      if (rangeStart < 0x1000) Serial.print('0');
      if (rangeStart < 0x0100) Serial.print('0');
      if (rangeStart < 0x0010) Serial.print('0');
      Serial.print(rangeStart, HEX);

      Serial.print(" - 0x");
      if (rangeEnd < 0x1000) Serial.print('0');
      if (rangeEnd < 0x0100) Serial.print('0');
      if (rangeEnd < 0x0010) Serial.print('0');
      Serial.print(rangeEnd, HEX);

      Serial.println(") belegt");
    }
  }

  Serial.print("\nInsgesamt ");
  Serial.print(usedCount);
  Serial.println(" belegte Pages von 256");
  Serial.println("=== ENDE PAGE BELEGUNGSCHECK ===");
  Serial.println();
}


// -----------------------------------------------------
void hexdump() {
  Serial.println("\n=== HEX DUMP START ===");
  
  if (!ds.reset()) return;
  ds.write(0xCC);
  ds.write(0xF0);
  ds.write(0x00);
  ds.write(0x00);
  
  for (uint16_t addr = 0; addr < 8192; addr += 16) {
    if (addr < 0x1000) Serial.print('0');
    if (addr < 0x0100) Serial.print('0');
    if (addr < 0x0010) Serial.print('0');
    Serial.print(addr, HEX);
    Serial.print(": ");
    
    for (int i = 0; i < 16; i++) {
      byte b = ds.read();
      printHexByte(b);
      Serial.print(' ');
    }
    Serial.println();
    
    if ((addr + 16) % 512 == 0) {
      Serial.print("# ");
      Serial.print((addr + 16) * 100 / 8192);
      Serial.println("%");
    }
  }
  
  Serial.println("=== HEX DUMP END ===");
}

// -----------------------------------------------------
void sendBinary() {
  Serial.println("BINARY_START");
  delay(100);
  
  if (!ds.reset()) {
    Serial.println("ERROR");
    return;
  }
  ds.write(0xCC);
  ds.write(0xF0);
  ds.write(0x00);
  ds.write(0x00);
  
  for (uint16_t i = 0; i < 8192; i++) {
    Serial.write(ds.read());
    if ((i + 1) % 64 == 0) delay(10);
  }
  
  delay(100);
  Serial.println("\nBINARY_END");
}

// -----------------------------------------------------
void sendStatus() {
  Serial.println("STATUS_START");
  delay(50);

  if (!ds.reset()) {
    Serial.println("ERROR_NO_DEVICE");
    return;
  }

  ds.write(0xCC);
  ds.write(0xAA);
  ds.write(0x00);
  ds.write(0x00);

  // 256 echte Bytes sammeln/senden
  for (uint8_t block = 0; block < 32; block++) {
    // 8 echte Statusbytes
    for (uint8_t i = 0; i < 8; i++) {
      byte b = ds.read();
      Serial.write(b);
    }
    // 2 CRC Bytes wegwerfen
    byte crcLo = ds.read();
    byte crcHi = ds.read();
    (void)crcLo;
    (void)crcHi;
  }

  delay(50);
  Serial.println("\nSTATUS_END");
}


// -----------------------------------------------------
void printHelp() {
  Serial.println("\n=== Kommandos ===");
  Serial.println("all       - Liest kompletten Speicher (formatiert)");
  Serial.println("pages     - Listet nur Pages, die nicht komplett 0xFF sind");
  Serial.println("hexdump   - Hex-Dump zum Kopieren");
  Serial.println("binary    - Binaerdaten senden (fuer Python-Script)");
  Serial.println("status    - Liest Status Memory");
  Serial.println("sendstatus- Status Memory binaer senden");
  Serial.println("rom       - Zeigt ROM Code");
  Serial.println("help      - Zeigt diese Hilfe");
  Serial.println("[ADRESSE] - Liest 64 Bytes ab Adresse (hex)");
  Serial.println();
  Serial.println("Python-Script Kommandos:");
  Serial.println("savebin   - Speichert binary.bin");
  Serial.println("savehex   - Speichert hexdump.hex");
  Serial.println("savestatus- Speichert status.bin");
  Serial.println("savefull  - Speichert alles");
  Serial.println();
}

// -----------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Serial Buffer leeren
  while (Serial.available() > 0) {
    Serial.read();
  }
  
  Serial.println("\n=== DS2506 EPROM Reader ===\n");
  
  if (!findDevice()) {
    Serial.println("Kein 1-Wire Device gefunden!");
    Serial.println("Pruefe:");
    Serial.println("- 4,7kOhm Pull-Up zwischen D4 und +5V");
    Serial.println("- DS2506 Pin 1+3 an GND, Pin 2 an D4");
    while (true);
  }
  
  printROM();
  Serial.println("\n=== Erste 128 Bytes ===");
  readMemory(0x0000, 128);
  printHelp();
  
  // Nach OneWire-Init kurz warten und Buffer leeren
  delay(100);
  while (Serial.available() > 0) {
    Serial.read();
  }
  
  Serial.println("Bereit fuer Befehle!");
}

// -----------------------------------------------------
void loop() {
  if (Serial.available() > 0) {
    // WICHTIG: Warte bis alle Zeichen angekommen sind!
    delay(50);
    
    // Lese alle verfügbaren Zeichen
    String input = "";
    while (Serial.available() > 0) {
      char c = Serial.read();
      if (c != '\n' && c != '\r') {
        input += c;
      }
    }
    
    // Verarbeite Eingabe
    input.trim();
    input.toLowerCase();
    
    if (input.length() > 0) {
      Serial.print("Befehl empfangen: '");
      Serial.print(input);
      Serial.println("'");
      
      if (input == "all") {
        readAllMemory();
        
      } else if (input == "hexdump") {
        hexdump();
        
      } else if (input == "binary") {
        sendBinary();
        
      } else if (input == "sendstatus") {
        sendStatus();
        
      } else if (input == "status") {
        readStatusMemory();
        
      } else if (input == "rom") {
        printROM();
        
      } else if (input == "help" || input == "?") {
        printHelp();

      } else if (input == "pages") {
        listUsedPages();

        
      } else {
        // Versuche als Hex-Adresse zu interpretieren
        uint16_t addr = strtol(input.c_str(), NULL, 16);
        if (addr < 8192 || input == "0") {
          Serial.print("\nLese 64 Bytes ab 0x");
          if (addr < 0x1000) Serial.print('0');
          if (addr < 0x0100) Serial.print('0');
          if (addr < 0x0010) Serial.print('0');
          Serial.println(addr, HEX);
          readMemory(addr, min(64, 8192 - addr));
        } else {
          Serial.print("Unbekannter Befehl: '");
          Serial.print(input);
          Serial.println("'");
          Serial.println("Gib 'help' ein fuer alle Kommandos");
        }
      }
    }
    
    Serial.println("\nBereit fuer naechstes Kommando:");
  }
}