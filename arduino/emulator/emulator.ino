#include <Arduino.h>
#include "OneWireHub.h"
#include "DS2506_Custom.h"


#define OW_PIN       4
#define FACTORY_PIN  2 // Taster nach GND


OneWireHub hub(OW_PIN);

// Virtueller DS2506-Chip
// ROM Code Original: 8B 52 EB 00 00 70 5E B9
// -> Wir übergeben Family + Seriennummer ohne die CRC (B9),
//    also sieben Bytes: 0x8B,0x52,0xEB,0x00,0x00,0x70,0x5E
DS2506_Custom chip(
    0x8B, 0x52, 0xEB, 0x00, 0x00, 0x70, 0x5E
);

static bool factoryPressedAtBoot() {
  pinMode(FACTORY_PIN, INPUT_PULLUP);
  delay(10);
  return digitalRead(FACTORY_PIN) == LOW;
}

void setup() {
#if DS2506_USE_EEPROM
  if (factoryPressedAtBoot() || chip.eepromLooksBlank()) {
    chip.eepromFactoryReset();   // BLOCKING: PROGMEM -> EEPROM -> RAM
  } else {
    chip.loadFromEEPROMToRAM();  // schnell
  }
#endif
  hub.attach(chip);              // sofort präsent
}

void loop() {
  hub.poll();          // 1-Wire bedienen
#if DS2506_USE_EEPROM
  chip.serviceBackground();  // bei Bus-Idle ein paar Bytes ins EEPROM committen
#endif
}
