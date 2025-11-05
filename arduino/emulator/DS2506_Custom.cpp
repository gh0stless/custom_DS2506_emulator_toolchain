#include "DS2506_Custom.h"
#include <avr/pgmspace.h>
#include <string.h>

#if DS2506_USE_EEPROM
  #include <avr/eeprom.h>
  static inline uint8_t eeRead(uint16_t a)              { return eeprom_read_byte((const uint8_t*)a); }
  static inline void    eeUpdate(uint16_t a, uint8_t v) { eeprom_update_byte((uint8_t*)a, v); }
#endif

// ---- Page-Mapping (wie gehabt) ----
struct PageMapEntry { uint8_t logical; uint8_t physical; };
static const PageMapEntry pageMap[DS2506_Custom::PHYS_PAGES] PROGMEM = {
    {  0, 0 }, { 16, 1 }, { 30, 2 }, { 38, 3 },
    { 48, 4 }, { 56, 5 }, { 63, 6 }, { 64, 7 },
};

// ---- CRC (invertiert) senden, ohne CRC-Akkumulator zu beeinflussen ----
void DS2506_Custom::sendCrc16Raw(OneWireHub* hub, uint16_t crc)
{
    uint16_t out = ~crc;
#if DS2506_SEND_TAKES_CRCREF
    uint16_t dummy = 0;
    hub->send(reinterpret_cast<uint8_t*>(&out), 2, dummy);
#else
    hub->send(reinterpret_cast<uint8_t*>(&out), 2);
#endif
}

// ---- ctor: RAM initial befüllen ----
DS2506_Custom::DS2506_Custom(uint8_t ID1, uint8_t ID2, uint8_t ID3,
                             uint8_t ID4, uint8_t ID5, uint8_t ID6, uint8_t ID7)
: OneWireItem(ID1, ID2, ID3, ID4, ID5, ID6, ID7)
{
#if DS2506_USE_EEPROM
    // Boot: möglichst flott präsent sein
    if (eepromLooksBlank()) {
        // Werksabbild (aus Flash) direkt in den RAM spiegeln
        memcpy_P(&memory[0 * PAGE_SIZE], page_0000, PAGE_SIZE);
        memcpy_P(&memory[1 * PAGE_SIZE], page_0200, PAGE_SIZE);
        memcpy_P(&memory[2 * PAGE_SIZE], page_03C0, PAGE_SIZE);
        memcpy_P(&memory[3 * PAGE_SIZE], page_04C0, PAGE_SIZE);
        memcpy_P(&memory[4 * PAGE_SIZE], page_0600, PAGE_SIZE);
        memcpy_P(&memory[5 * PAGE_SIZE], page_0700, PAGE_SIZE);
        memcpy_P(&memory[6 * PAGE_SIZE], page_07E0, PAGE_SIZE);
        memcpy_P(&memory[7 * PAGE_SIZE], page_0800, PAGE_SIZE);
        memcpy_P(status_ram, status_mem, STATUS_SIZE_EMU);
    } else {
        loadFromEEPROMToRAM();
    }
#else
    memcpy_P(&memory[0 * PAGE_SIZE], page_0000, PAGE_SIZE);
    memcpy_P(&memory[1 * PAGE_SIZE], page_0200, PAGE_SIZE);
    memcpy_P(&memory[2 * PAGE_SIZE], page_03C0, PAGE_SIZE);
    memcpy_P(&memory[3 * PAGE_SIZE], page_04C0, PAGE_SIZE);
    memcpy_P(&memory[4 * PAGE_SIZE], page_0600, PAGE_SIZE);
    memcpy_P(&memory[5 * PAGE_SIZE], page_0700, PAGE_SIZE);
    memcpy_P(&memory[6 * PAGE_SIZE], page_07E0, PAGE_SIZE);
    memcpy_P(&memory[7 * PAGE_SIZE], page_0800, PAGE_SIZE);
    memcpy_P(status_ram, status_mem, STATUS_SIZE_EMU);
#endif
}

// ---- Mapping ----
int8_t DS2506_Custom::logicalToPhysicalPage(uint8_t logicalPage)
{
    for (uint8_t i=0;i<PHYS_PAGES;i++) {
        PageMapEntry e; memcpy_P(&e, &pageMap[i], sizeof(e));
        if (e.logical == logicalPage) return (int8_t)e.physical;
    }
    return -1;
}
uint16_t DS2506_Custom::mapAddressToPhysical(uint16_t dsAddr) const
{
    const uint8_t logical = uint8_t(dsAddr >> 5);
    const uint8_t off     = uint8_t(dsAddr & PAGE_MASK);
    const int8_t phys     = logicalToPhysicalPage(logical);
    if (phys < 0) return 0xFFFF;
    return uint16_t(phys) * PAGE_SIZE + off;
}

// ---- Hauptdienst ----
void DS2506_Custom::duty(OneWireHub * const hub)
{
    uint8_t  cmd;
    uint16_t reg_TA;
    uint16_t reg_RA = 0;
    uint16_t crc    = 0;

    if (hub->recv(&cmd, 1, crc)) return;
    if (hub->recv(reinterpret_cast<uint8_t*>(&reg_TA), 2, crc)) return;
    markBusUse();

    switch (cmd)
    {
        // -------- 0xF0 : READ MEMORY --------
        case 0xF0:
        {
            while (reg_TA < DEVICE_TOTAL_SIZE)
            {
                uint8_t  pageRemain = PAGE_SIZE - uint8_t(reg_TA & PAGE_MASK);
                uint16_t devRemain  = DEVICE_TOTAL_SIZE - reg_TA;
                uint8_t  chunk      = (pageRemain > devRemain) ? uint8_t(devRemain) : pageRemain;

                while (chunk--) {
                    const uint16_t phys = mapAddressToPhysical(reg_TA);
                    const uint8_t  data = (phys != 0xFFFF) ? memory[phys] : 0xFF;
                    if (hub->send(&data, 1, crc)) return;
                    reg_TA++;
                    markBusUse();
                }
            }
            sendCrc16Raw(hub, crc);
            markBusUse();
        } break;

        // -------- 0xAA : READ STATUS (je 8 B + CRC, dann CRC=0) --------
        case 0xAA:
        {
            while (reg_TA < STATUS_SIZE_EMU)
            {
                reg_RA = reg_TA & uint8_t(7);
                while (reg_RA < 8 && reg_TA < STATUS_SIZE_EMU)
                {
                    const uint8_t data = readStatusByte(reg_TA);
                    if (hub->send(&data, 1, crc)) return;
                    reg_RA++; reg_TA++;
                    markBusUse();
                }
                sendCrc16Raw(hub, crc);
                crc = 0;
                markBusUse();
            }
        } break;

#if DS2506_ENABLE_WRITE
        // -------- 0x0F : WRITE MEMORY (OTP 1->0), Abschluss: CRC senden --------
        case 0x0F:
        {
            while (true) {
                uint8_t incoming;
                if (hub->recv(&incoming, 1, crc)) break; // Ende → CRC zurück
                const uint16_t phys = mapAddressToPhysical(reg_TA);
                if (phys == 0xFFFF) {
#if DS2506_STRICT_ADDR_CHECK
                    hub->raiseSlaveError(0x0F); return;
#endif
                } else {
                    const uint8_t old = memory[phys];
                    const uint8_t burned = old & incoming;
                    if (burned != old) {
                        memory[phys] = burned;       // RAM sofort
#if DS2506_USE_EEPROM
                        markDataDirty(phys);         // EEPROM später
#endif
                    }
                }
                reg_TA++;
                markBusUse();
            }
            // 1) CRC sofort zurücksenden (Master bleibt synchron)
            sendCrc16Raw(hub, crc);
            markBusUse();
            // 2) Commit später im serviceBackground()
        } break;

        // -------- 0x55 : WRITE STATUS (OTP 1->0), Abschluss: CRC senden --------
        case 0x55:
        {
            while (true) {
                uint8_t incoming;
                if (hub->recv(&incoming, 1, crc)) break; // Ende → CRC zurück

                if (reg_TA < STATUS_SIZE_EMU) {
                    const uint8_t old = status_ram[reg_TA];
                    const uint8_t burned = old & incoming;
                    if (burned != old) {
                        status_ram[reg_TA] = burned;   // RAM sofort
#if DS2506_USE_EEPROM
                        markStatDirty(reg_TA);         // EEPROM später
#endif
                    }
                } else {
#if DS2506_STRICT_ADDR_CHECK
                    hub->raiseSlaveError(0x55); return;
#endif
                }
                reg_TA++;
                markBusUse();
            }
            sendCrc16Raw(hub, crc);
            markBusUse();
            // Commit später im serviceBackground()
        } break;
#endif // DS2506_ENABLE_WRITE

        default:
            hub->raiseSlaveError(cmd);
            break;
    }
}

#if DS2506_USE_EEPROM
// ---- Werksreset (blocking) ----
void DS2506_Custom::eepromFactoryReset()
{
    for (uint8_t i=0;i<PAGE_SIZE;i++) eeUpdate(EEPROM_MEM_BASE + 0*32 + i, pgm_read_byte(&page_0000[i]));
    for (uint8_t i=0;i<PAGE_SIZE;i++) eeUpdate(EEPROM_MEM_BASE + 1*32 + i, pgm_read_byte(&page_0200[i]));
    for (uint8_t i=0;i<PAGE_SIZE;i++) eeUpdate(EEPROM_MEM_BASE + 2*32 + i, pgm_read_byte(&page_03C0[i]));
    for (uint8_t i=0;i<PAGE_SIZE;i++) eeUpdate(EEPROM_MEM_BASE + 3*32 + i, pgm_read_byte(&page_04C0[i]));
    for (uint8_t i=0;i<PAGE_SIZE;i++) eeUpdate(EEPROM_MEM_BASE + 4*32 + i, pgm_read_byte(&page_0600[i]));
    for (uint8_t i=0;i<PAGE_SIZE;i++) eeUpdate(EEPROM_MEM_BASE + 5*32 + i, pgm_read_byte(&page_0700[i]));
    for (uint8_t i=0;i<PAGE_SIZE;i++) eeUpdate(EEPROM_MEM_BASE + 6*32 + i, pgm_read_byte(&page_07E0[i]));
    for (uint8_t i=0;i<PAGE_SIZE;i++) eeUpdate(EEPROM_MEM_BASE + 7*32 + i, pgm_read_byte(&page_0800[i]));
    for (uint16_t i=0;i<STATUS_SIZE_EMU;i++) eeUpdate(EEPROM_STAT_BASE + i, pgm_read_byte(&status_mem[i]));
    loadFromEEPROMToRAM();

    // nach einem harten Reset ist nichts dirty
    dataDirty_ = statDirty_ = false;
    dataDirtyLo_ = statDirtyLo_ = 0xFFFF;
    dataDirtyHi_ = statDirtyHi_ = 0;
    committing_ = false;
}

// ---- Laden EEPROM -> RAM (schnell) ----
void DS2506_Custom::loadFromEEPROMToRAM()
{
    for (uint16_t i=0;i<MEM_SIZE;i++)         memory[i]     = eeRead(EEPROM_MEM_BASE + i);
    for (uint16_t i=0;i<STATUS_SIZE_EMU;i++)  status_ram[i] = eeRead(EEPROM_STAT_BASE + i);
}

// ---- „Blank“-Heuristik ----
bool DS2506_Custom::eepromLooksBlank(uint8_t sample) const
{
    for (uint8_t i=0;i<sample;i++) if (eeRead(EEPROM_MEM_BASE  + i) != 0xFF) return false;
    for (uint8_t i=0;i<sample;i++) if (eeRead(EEPROM_STAT_BASE + i) != 0xFF) return false;
    return true;
}

// ---- Hintergrundservice: erst wenn Bus idle, ein paar Bytes committen ----
void DS2506_Custom::startCommitIfNeeded_()
{
    if (committing_) return;

    if (dataDirty_) { commitWhichIsData_ = true;  commitPos_ = dataDirtyLo_; committing_ = true; return; }
    if (statDirty_) { commitWhichIsData_ = false; commitPos_ = statDirtyLo_; committing_ = true; return; }
}

void DS2506_Custom::commitStep_(uint16_t budget)
{
    if (!committing_) return;

    if (commitWhichIsData_) {
        uint16_t end = dataDirtyHi_;
        while (budget && commitPos_ <= end) {
            uint8_t v = memory[commitPos_];
            uint8_t old = eeRead(EEPROM_MEM_BASE + commitPos_);
            if (old != v) eeUpdate(EEPROM_MEM_BASE + commitPos_, v);
            commitPos_++; budget--;
        }
        if (commitPos_ > end) {
            dataDirty_ = false;
            dataDirtyLo_ = 0xFFFF; dataDirtyHi_ = 0;
            committing_ = false;
        }
    } else {
        uint16_t end = statDirtyHi_;
        while (budget && commitPos_ <= end) {
            uint8_t v = status_ram[commitPos_];
            uint8_t old = eeRead(EEPROM_STAT_BASE + commitPos_);
            if (old != v) eeUpdate(EEPROM_STAT_BASE + commitPos_, v);
            commitPos_++; budget--;
        }
        if (commitPos_ > end) {
            statDirty_ = false;
            statDirtyLo_ = 0xFFFF; statDirtyHi_ = 0;
            committing_ = false;
        }
    }
}

void DS2506_Custom::serviceBackground()
{
    // nur schreiben, wenn der Bus gerade Ruhe hat
    if (!busIdle()) return;
    startCommitIfNeeded_();
    commitStep_(DS2506_COMMIT_BUDGET_BYTES);
}
#endif // DS2506_USE_EEPROM
