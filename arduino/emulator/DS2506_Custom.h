#pragma once
#include <Arduino.h>
#include "OneWireItem.h"
#include "OneWireHub.h"
#include "ds2506_image.h"   // page_0000..page_0800 + status_mem (PROGMEM)

// =================== Konfiguration ===================

// Schreibfunktion (OTP 1->0) an/aus
#ifndef DS2506_ENABLE_WRITE
#define DS2506_ENABLE_WRITE 1
#endif

// Ungültige Adressen: 0 = tolerant (ignorieren), 1 = sofort Fehler
#ifndef DS2506_STRICT_ADDR_CHECK
#define DS2506_STRICT_ADDR_CHECK 0
#endif

// EEPROM-Persistenz: 1 = RAM + (hintergründiges) EEPROM, 0 = nur RAM
#ifndef DS2506_USE_EEPROM
#define DS2506_USE_EEPROM 1
#endif

// OneWireHub::send-Overload?
// 0 = send(ptr,len) verfügbar; 1 = nur send(ptr,len,crcRef) verfügbar
#ifndef DS2506_SEND_TAKES_CRCREF
#define DS2506_SEND_TAKES_CRCREF 0
#endif

// Hintergrund-Commit: ab welcher Bus-Idle-Zeit (ms) und wie viele Bytes pro Service
#ifndef DS2506_IDLE_MS_BEFORE_COMMIT
#define DS2506_IDLE_MS_BEFORE_COMMIT 20
#endif
#ifndef DS2506_COMMIT_BUDGET_BYTES
#define DS2506_COMMIT_BUDGET_BYTES 8
#endif

// =================== Klasse ===================

class DS2506_Custom : public OneWireItem
{
public:
    // DS2506 Geometrie (nur fürs Clamping)
    static constexpr uint16_t DEVICE_TOTAL_SIZE = 8192; // 0x2000
    static constexpr uint8_t  PAGE_SIZE  = 32;
    static constexpr uint8_t  PAGE_MASK  = 0x1F;

    // emulierte Datenmenge (8 belegte Pages à 32 B)
    static constexpr uint16_t MEM_SIZE   = 256;
    static constexpr uint8_t  PHYS_PAGES = MEM_SIZE / PAGE_SIZE;

    // Statusbereich (dump) = 256 B
    static constexpr uint16_t STATUS_SIZE_EMU = 256;

#if DS2506_USE_EEPROM
    // EEPROM-Layout (Tiny85: 512 B → passt genau)
    static constexpr uint16_t EEPROM_MEM_BASE  = 0;     // 0..255  : Daten
    static constexpr uint16_t EEPROM_STAT_BASE = 256;   // 256..511: Status
#endif

    // 7-Byte-ROM-ID (Family + 6x SN) kommt aus dem Sketch
    DS2506_Custom(uint8_t ID1, uint8_t ID2, uint8_t ID3,
                  uint8_t ID4, uint8_t ID5, uint8_t ID6, uint8_t ID7);

    // 1-Wire-Dienst (nach MATCH/SKIP ROM vom Hub aufgerufen)
    void duty(OneWireHub * hub) override;

#if DS2506_USE_EEPROM
    // ---- EEPROM-Unterstützung ----
    // Werksreset (blocking): PROGMEM -> EEPROM, dann EEPROM -> RAM
    void eepromFactoryReset();

    // Laden (schnell, presence-sicher): EEPROM -> RAM
    void loadFromEEPROMToRAM();

    // Heuristik: wirkt EEPROM (Daten+Status) „blank“ (0xFF)?
    bool eepromLooksBlank(uint8_t sample = 16) const;

    // Hintergrundservice: bei Bus-Idle einige Dirty-Bytes in EEPROM committen
    void serviceBackground();
#endif

private:
    // -------- RAM-Spiegel (immer) --------
    uint8_t memory[MEM_SIZE];                 // 256 B Daten
    uint8_t status_ram[STATUS_SIZE_EMU];      // 256 B Status

    // -------- Mapping & Utils --------
    // logische Page -> physische (0..7), -1 wenn nicht gemappt
    static int8_t  logicalToPhysicalPage(uint8_t logicalPage);

    // DS-Adressraum (0..0x1FFF) -> Index 0..255 (unser Fenster), 0xFFFF wenn leer
    uint16_t       mapAddressToPhysical(uint16_t dsAddr) const;

    // Status-Byte aus RAM (Bounds-check inline)
    inline uint8_t readStatusByte(uint16_t a) const {
        return (a < STATUS_SIZE_EMU) ? status_ram[a] : 0xFF;
    }

    // invertierte CRC16 senden, ohne den laufenden CRC-Akkumulator zu verändern
    static void    sendCrc16Raw(OneWireHub* hub, uint16_t crc);

#if DS2506_ENABLE_WRITE
    // OTP-Write in die RAM-Spiegel (Persistenz ggf. separat)
    void programDataByte(uint16_t dsAddr, uint8_t newVal);
    void programStatusByte(uint16_t dsAddr, uint8_t newVal);
#endif

    // -------- Bus-Idle-Tracking: immer vorhanden (bei USE_EEPROM=0 als No-Op) --------
    inline void markBusUse() {
    #if DS2506_USE_EEPROM
        lastBusUseMs_ = millis();
    #else
        // no-op
    #endif
    }
    inline bool busIdle() const {
    #if DS2506_USE_EEPROM
        return (millis() - lastBusUseMs_) >= DS2506_IDLE_MS_BEFORE_COMMIT;
    #else
        return true; // wird ohne EEPROM nicht genutzt
    #endif
    }

#if DS2506_USE_EEPROM
    // -------- Commit-Tracking (kompakt; ohne große Bitmaps) --------
    bool     dataDirty_     = false;
    uint16_t dataDirtyLo_   = 0xFFFF, dataDirtyHi_ = 0;
    bool     statDirty_     = false;
    uint16_t statDirtyLo_   = 0xFFFF, statDirtyHi_ = 0;

    // Commit-Fortschritt über mehrere service()-Aufrufe
    bool     committing_    = false;
    bool     commitWhichIsData_ = true;   // abwechselnd Daten/Status
    uint16_t commitPos_     = 0;

    // Zeitstempel letzte Bus-Nutzung
    uint32_t lastBusUseMs_  = 0;

    // interne Helfer für Commit-Sequenz
    void startCommitIfNeeded_();
    void commitStep_(uint16_t budgetBytes);

    // Dirty-Markierungen setzen
    inline void markDataDirty(uint16_t idx) {
        dataDirty_ = true;
        if (idx < dataDirtyLo_) dataDirtyLo_ = idx;
        if (idx > dataDirtyHi_) dataDirtyHi_ = idx;
    }
    inline void markStatDirty(uint16_t idx) {
        statDirty_ = true;
        if (idx < statDirtyLo_) statDirtyLo_ = idx;
        if (idx > statDirtyHi_) statDirtyHi_ = idx;
    }
#endif
};
