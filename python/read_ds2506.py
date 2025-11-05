#!/usr/bin/env python3
import serial
import sys
import time
import os


class DS2506Reader:
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.memory_size = 8192  # 8 kB Dumpgröße

    # -------------------------------------------------
    # Serielle Verbindung aufbauen / schließen
    def connect(self):
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"Verbunden mit {self.port} @ {self.baudrate} baud")
            time.sleep(2)

            # Begrüßungslinien vom Arduino leerlesen
            time.sleep(0.5)
            while self.ser.in_waiting:
                line = self.ser.readline().decode("utf-8", errors="ignore").rstrip()
                if line:
                    print(line)

            return True
        except serial.SerialException as e:
            print(f"Fehler beim Verbinden: {e}")
            return False

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("Verbindung geschlossen")

    # -------------------------------------------------
    # Roh-Kommando an den Arduino schicken + Text lesen
    def send_command(self, cmd):
        if not self.ser or not self.ser.is_open:
            print("Nicht verbunden!")
            return None

        self.ser.write(f"{cmd}\n".encode())
        time.sleep(0.1)

        output = []
        while True:
            if self.ser.in_waiting:
                line = self.ser.readline().decode("utf-8", errors="ignore").rstrip()
                if line:
                    print(line)
                    output.append(line)
            else:
                time.sleep(0.1)
                if not self.ser.in_waiting:
                    break

        return output

    # -------------------------------------------------
    # CRC8 (Dallas/Maxim) für ROM-Code
    def compute_crc8_maxim(self, data_bytes):
        crc = 0
        for byte in data_bytes:
            crc ^= byte
            for _ in range(8):
                if crc & 0x01:
                    crc = (crc >> 1) ^ 0x8C
                else:
                    crc >>= 1
            crc &= 0xFF
        return crc

    def get_rom_info(self):
        lines = self.send_command("rom")
        rom_bytes = []

        for line in lines:
            if line.startswith("ROM Code"):
                parts = line.split(":", 1)
                if len(parts) == 2:
                    hexlist = parts[1].strip().split()
                else:
                    hexlist = line.replace("ROM Code", "").strip().split()
                for p in hexlist:
                    try:
                        rom_bytes.append(int(p, 16))
                    except ValueError:
                        pass

        info = {}
        print("\n=== ROM Code Analyse (Python) ===")
        if len(rom_bytes) == 8:
            calc_crc = self.compute_crc8_maxim(rom_bytes[:7])
            chip_crc = rom_bytes[7]
            crc_ok = (calc_crc == chip_crc)

            print("ROM Bytes:", " ".join(f"{b:02X}" for b in rom_bytes))
            print(f"Family Code: 0x{rom_bytes[0]:02X}")
            print(f"CRC (Chip / Byte7): 0x{chip_crc:02X}")
            print(f"CRC (berechnet):    0x{calc_crc:02X}")
            print("CRC Status:", "OK" if crc_ok else "FEHLER!")

            info = {
                "rom_bytes": rom_bytes,
                "family_code": rom_bytes[0],
                "crc_chip": chip_crc,
                "crc_calc": calc_crc,
                "crc_ok": crc_ok,
            }
        else:
            print("Konnte ROM Code nicht sauber parsen (nicht exakt 8 Bytes).")

        return info

    # -------------------------------------------------
    # 8 kB Data Memory holen
    #
    # Arduino-Protokoll:
    #   BINARY_START (oder BIN_START)
    #   <8192 rohe Bytes via Serial.write()>
    #   BINARY_END   (oder BIN_END)
    #
    def read_binary_data(self):
        if not self.ser or not self.ser.is_open:
            print("Nicht verbunden!")
            return None

        print("Sende 'binary' Kommando...")
        self.ser.write(b"binary\n")

        start_marker = None
        end_marker = None
        timeout = time.time() + 5

        # auf Start-Marker warten
        while time.time() < timeout:
            if self.ser.in_waiting:
                line = self.ser.readline().decode("utf-8", errors="ignore").strip()
                if line:
                    print(f"< {line}")
                if line == "BINARY_START":
                    start_marker = "BINARY_START"
                    end_marker = "BINARY_END"
                    break
                if line == "BIN_START":
                    start_marker = "BIN_START"
                    end_marker = "BIN_END"
                    break
                if "ERROR" in line:
                    print("Fehler beim Lesen!")
                    return None
            else:
                time.sleep(0.05)

        if start_marker is None:
            print("Timeout/kein START-Marker erkannt (BINARY_START / BIN_START).")
            return None

        print(f"Empfange Data Memory ({self.memory_size} Bytes)...")
        data = bytearray()
        start_time = time.time()

        while len(data) < self.memory_size:
            chunk = self.ser.read(self.memory_size - len(data))
            if chunk:
                data.extend(chunk)

            progress = len(data) * 100 // self.memory_size
            elapsed = time.time() - start_time
            print(
                f"\rFortschritt: {progress}% ({len(data)}/{self.memory_size}) - {elapsed:.1f}s",
                end="",
            )

        print("\nFertig! Länge empfangen:", len(data))

        # Rest lesen bis END-Marker (optional)
        timeout2 = time.time() + 2
        while time.time() < timeout2 and self.ser.in_waiting == 0:
            time.sleep(0.05)

        while self.ser.in_waiting:
            line = self.ser.readline().decode("utf-8", errors="ignore").strip()
            if line:
                print(f"< {line}")
            if end_marker and line == end_marker:
                print("Ende-Marker erkannt:", end_marker)

        if len(data) != self.memory_size:
            print(f"WARNUNG: Falsche Länge ({len(data)} statt {self.memory_size})")
            return None

        return bytes(data)

    # -------------------------------------------------
    # 256 Byte Status Memory holen
    #
    # Arduino-Protokoll:
    #   STATUS_START (oder STATUS_BEGIN)
    #   <256 rohe Bytes via Serial.write()>
    #   STATUS_END
    #
    def read_status_data(self):
        if not self.ser or not self.ser.is_open:
            print("Nicht verbunden!")
            return None

        print("Sende 'sendstatus' Kommando...")
        self.ser.write(b"sendstatus\n")

        start_marker = None
        end_marker = "STATUS_END"
        timeout = time.time() + 5

        # auf Startmarker warten
        while time.time() < timeout:
            if self.ser.in_waiting:
                line = self.ser.readline().decode("utf-8", errors="ignore").strip()
                if line:
                    print(f"< {line}")
                if line == "STATUS_START":
                    start_marker = "STATUS_START"
                    break
                if line == "STATUS_BEGIN":
                    start_marker = "STATUS_BEGIN"
                    break
                if "ERROR" in line:
                    print("Fehler beim Lesen!")
                    return None
            else:
                time.sleep(0.05)

        if start_marker is None:
            print("Timeout/kein STATUS_START (oder STATUS_BEGIN) erkannt.")
            return None

        print("Empfange Status Memory (256 Bytes)...")
        data = bytearray()
        start_time = time.time()
        while len(data) < 256:
            chunk = self.ser.read(256 - len(data))
            if chunk:
                data.extend(chunk)

            progress = len(data) * 100 // 256
            elapsed = time.time() - start_time
            print(
                f"\rFortschritt: {progress}% ({len(data)}/256) - {elapsed:.1f}s",
                end="",
            )

        print("\nFertig! Länge empfangen:", len(data))

        # END-Marker einsammeln (optional)
        timeout2 = time.time() + 2
        while time.time() < timeout2 and self.ser.in_waiting == 0:
            time.sleep(0.05)

        while self.ser.in_waiting:
            line = self.ser.readline().decode("utf-8", errors="ignore").strip()
            if line:
                print(f"< {line}")
            if line == end_marker:
                print("Ende-Marker erkannt:", end_marker)

        if len(data) != 256:
            print(f"WARNUNG: Falsche Status-Länge ({len(data)} statt 256)")
            return None

        return bytes(data)

    # -------------------------------------------------
    # Dateien speichern (mit optional benanntem Dateinamen)
    def save_binary(self, data, filename="binary.bin"):
        with open(filename, "wb") as f:
            f.write(data)
        print(f"✓ Gespeichert: {filename} ({len(data)} bytes)")
        return filename

    def save_status(self, data, filename="status.bin"):
        with open(filename, "wb") as f:
            f.write(data)
        print(f"✓ Gespeichert: {filename} ({len(data)} bytes)")
        return filename

    def save_hexdump(self, data, filename="hexdump.hex"):
        with open(filename, "w") as f:
            for addr in range(0, len(data), 16):
                chunk = data[addr:addr + 16]
                hex_str = " ".join(f"{b:02x}" for b in chunk)
                ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
                f.write(f"{addr:04x}: {hex_str:<48} {ascii_str}\n")
        print(f"✓ Gespeichert: {filename}")
        return filename

    # -------------------------------------------------
    # Helfer für Analyse / Report
    def _format_page_ranges(self, pages):
        if not pages:
            return "(keine)"
        pages = sorted(pages)
        ranges = []
        start = pages[0]
        last = pages[0]
        for p in pages[1:]:
            if p == last + 1:
                last = p
            else:
                ranges.append((start, last))
                start = p
                last = p
        ranges.append((start, last))

        out = []
        for a, b in ranges:
            if a == b:
                out.append(str(a))
            else:
                out.append(f"{a}-{b}")
        return ",".join(out)

    def _hexdump_lines(self, data, start_addr=0):
        lines = []
        for offs in range(0, len(data), 16):
            chunk = data[offs:offs + 16]
            hex_str = " ".join(f"{b:02X}" for b in chunk)
            ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            lines.append(f"{start_addr+offs:04X}: {hex_str:<48} {ascii_str}")
        return lines

    def _hexdump_page_32bytes(self, data, start_addr):
        lines = []
        for offs in range(0, 32, 16):
            addr = start_addr + offs
            chunk = data[start_addr + offs:start_addr + offs + 16]
            hex_str = " ".join(f"{b:02X}" for b in chunk)
            ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            lines.append(f"  {addr:04X}: {hex_str:<48} {ascii_str}")
        return lines

    # -------------------------------------------------
    # Status interpretieren (Schreibschutz usw.)
    def analyze_status(self, data):
        lines_out = []

        def add(msg=""):
            print(msg)
            lines_out.append(msg)

        add("\n=== Status Memory Analyse (Python) ===")

        # Write Protect Bits (Byte 0x00..0x1F, Bit = 0 => gesperrt)
        wp_pages = []
        for page in range(256):
            byte_i = page // 8
            bit_i = page % 8
            if byte_i < 0x20:
                if not (data[byte_i] & (1 << bit_i)):
                    wp_pages.append(page)

        add(f"Write-Protected Pages: {len(wp_pages)}/256")
        add("  Seiten gesperrt: " + self._format_page_ranges(wp_pages))

        # Redirection/EPROM Info 0x20..0x3F
        eprom_bytes = data[0x20:0x40]
        eprom_count = sum(1 for b in eprom_bytes if b != 0xFF)
        add(f"EPROM/Redirection gesetzt (Bytes 0x20-0x3F != FF): {eprom_count}/32")
        add("  Redirection/EPROM Bytes:")
        add("   " + " ".join(f"{b:02X}" for b in eprom_bytes))

        # Copy-Protect Bits (heuristisch) ab 0x100
        cp_pages = []
        for page in range(256):
            byte_i = 0x100 + (page // 8)
            bit_i = page % 8
            if byte_i < len(data):
                if not (data[byte_i] & (1 << bit_i)):
                    cp_pages.append(page)

        add(f"Copy-Protected Pages: {len(cp_pages)}/256")
        add("  Copy-geschützt: " + self._format_page_ranges(cp_pages))

        add("=== Ende Status Analyse ===")
        add()
        return "\n".join(lines_out)

    # -------------------------------------------------
    # Belegte Pages (Data Memory) bestimmen
    def calc_used_pages_from_binary(self, data):
        PAGE_SIZE = 32
        TOTAL_SIZE = len(data)
        page_count = TOTAL_SIZE // PAGE_SIZE

        used_pages = []
        page_hexdump_map = {}

        print("\n=== PAGE BELEGUNG (Python) ===")

        for page in range(page_count):
            start = page * PAGE_SIZE
            end = start + PAGE_SIZE
            chunk = data[start:end]

            if any(b != 0xFF for b in chunk):
                used_pages.append(page)

                range_start = start
                range_end = start + PAGE_SIZE - 1

                if page < 10:
                    page_str = f"00{page}"
                elif page < 100:
                    page_str = f"0{page}"
                else:
                    page_str = f"{page}"

                print(f"Page {page_str} (0x{range_start:04X} - 0x{range_end:04X}) belegt")

                page_lines = self._hexdump_page_32bytes(data, range_start)
                for line in page_lines:
                    print(line)

                page_hexdump_map[page] = page_lines

        print()
        print(f"Insgesamt {len(used_pages)} belegte Pages von {page_count}")
        print("=== ENDE PAGE BELEGUNG (Python) ===\n")

        return used_pages, page_hexdump_map

    # -------------------------------------------------
    # Präfix für Dateinamen bauen
    #
    # Format endgültig:
    #   <Geraetenummer_bereinigt>_<UserTag>_<Zulassungsnummer_DEC>
    #
    # Geraetenummer:
    #   Bytes 0x07EC..0x07EF als ASCII.
    #   Sonderfall:
    #     Wenn Byte 0x07EC == 'G' (0x47), diese führende 'G' NICHT übernehmen.
    #     Dann nur 07ED..07EF (3 Zeichen) nehmen.
    #   Sonst alle 4 Bytes nehmen.
    #   0x00 / 0xFF werden ignoriert.
    #   Undruckbares -> "_".
    #   Leer -> "UNKDEV".
    #
    # UserTag:
    #   vom Benutzer eingegeben; nur [A-Za-z0-9_-] bleibt.
    #   Rest wird "_".
    #
    # Zulassungsnummer:
    #   Bytes 0x07F2..0x07F5 als BIG-ENDIAN 32-bit Integer
    #   (b0<<24 | b1<<16 | b2<<8 | b3), dann dezimal.
    #   Wenn alles 0x00 oder alles 0xFF -> "UNKZUL".
    #
    def build_prefix(self, binary_data, user_tag):
        if not binary_data or len(binary_data) <= 0x07F5:
            print("WARNUNG: Dump zu klein, kann Prefix nicht bilden.")
            dev_ascii = "UNKDEV"
            zul_str = "UNKZUL"
        else:
            # Gerätenummer: 0x07EC..0x07EF
            raw_dev_all4 = binary_data[0x07EC:0x07F0]  # 4 Bytes

            if len(raw_dev_all4) == 4 and raw_dev_all4[0] == 0x47:  # 'G'?
                relevant_bytes = raw_dev_all4[1:4]  # nur die letzten 3
            else:
                relevant_bytes = raw_dev_all4[:]    # alle 4

            dev_chars = []
            for b in relevant_bytes:
                if b == 0x00 or b == 0xFF:
                    # Füllbytes ignorieren
                    continue
                if 32 <= b <= 126:
                    dev_chars.append(chr(b))
                else:
                    dev_chars.append("_")

            dev_ascii = "".join(dev_chars).strip()
            if dev_ascii == "":
                dev_ascii = "UNKDEV"

            # Zulassungsnummer (0x07F2..0x07F5), Big Endian -> Dezimal
            zul_raw = binary_data[0x07F2:0x07F6]  # 4 Bytes
            if len(zul_raw) == 4:
                if all(b == 0xFF for b in zul_raw) or all(b == 0x00 for b in zul_raw):
                    zul_str = "UNKZUL"
                else:
                    zul_val = (
                        ((zul_raw[0] & 0xFF) << 24)
                        | ((zul_raw[1] & 0xFF) << 16)
                        | ((zul_raw[2] & 0xFF) << 8)
                        | (zul_raw[3] & 0xFF)
                    )
                    zul_str = str(zul_val)
            else:
                zul_str = "UNKZUL"

        # User-Tag bereinigen
        safe_tag = "".join(
            ch if (ch.isalnum() or ch in "-_") else "_" for ch in user_tag
        )
        
        # --- UserTag bereinigen ---
        # Punkt '.' ist jetzt ausdrücklich erlaubt und bleibt erhalten.
        safe_tag = "".join(
            ch if (ch.isalnum() or ch in "-_.") else "_" for ch in user_tag
        )

        # --- Präfix-Schema ---
        # <geraet>_<tag>_<zulassung>
        prefix = f"{dev_ascii}_{safe_tag}_{zul_str}"

        print(f"Datei-Präfix: {prefix}")
        return prefix

    # -------------------------------------------------
    # Report-/Analyse-Datei schreiben
    def save_full_report(
        self,
        rom_info,
        binary_data,
        status_data,
        status_analysis_text,
        used_pages=None,
        page_hexdump_map=None,
        filename="dump_report.txt",
    ):
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S")

        with open(filename, "w") as f:
            f.write("DS2506 Dump Report\n")
            f.write(f"Erstellt: {timestamp}\n\n")

            # ROM Info
            f.write("=== ROM Code ===\n")
            if rom_info and "rom_bytes" in rom_info:
                f.write(
                    "ROM Bytes: "
                    + " ".join(f"{b:02X}" for b in rom_info["rom_bytes"])
                    + "\n"
                )
                f.write(f"Family Code: 0x{rom_info['family_code']:02X}\n")
                f.write(f"CRC (Chip):      0x{rom_info['crc_chip']:02X}\n")
                f.write(f"CRC (berechnet): 0x{rom_info['crc_calc']:02X}\n")
                f.write(
                    "CRC Status: "
                    + ("OK" if rom_info["crc_ok"] else "FEHLER!")
                    + "\n"
                )
            else:
                f.write("ROM Code konnte nicht ermittelt werden.\n")

            f.write("\n")

            # belegte Pages
            if binary_data and used_pages is not None:
                f.write("=== BELEGTE PAGES (mind. 1 Byte != FF) ===\n")
                for page in used_pages:
                    start = page * 32
                    end = start + 31
                    f.write(
                        f"Page {page:03d} (0x{start:04X} - 0x{end:04X}) belegt\n"
                    )
                    if page_hexdump_map and page in page_hexdump_map:
                        for line in page_hexdump_map[page]:
                            f.write(line + "\n")
                    f.write("\n")
                f.write(
                    f"Insgesamt {len(used_pages)} belegte Pages von {len(binary_data)//32}\n\n"
                )

            # kompletter Datenspeicher (Hexdump)
            if binary_data:
                f.write("=== DATA MEMORY HEXDUMP (8192 Bytes) ===\n")
                for line in self._hexdump_lines(binary_data, start_addr=0):
                    f.write(line + "\n")
                f.write("\n")
            else:
                f.write("Keine Data Memory Daten verfügbar.\n\n")

            # Statusspeicher (Hexdump)
            if status_data:
                f.write("=== STATUS MEMORY HEXDUMP (256 Bytes) ===\n")
                for line in self._hexdump_lines(status_data, start_addr=0):
                    f.write(line + "\n")
                f.write("\n")
            else:
                f.write("Keine Status Memory Daten verfügbar.\n\n")

            if status_analysis_text:
                f.write(status_analysis_text)
                f.write("\n")

        print(f"✓ Gespeichert: {filename}")
        return filename

    # -------------------------------------------------
    # ds2506_image.h erzeugen
    def _format_c_array_page(self, name, chunk, start_addr):
        lines_out = []
        lines_out.append(f"// Page @ 0x{start_addr:04X} - 0x{start_addr+31:04X}")
        lines_out.append(f"const uint8_t {name}[32] PROGMEM = {{")
        for i in range(0, 32, 8):
            part = chunk[i:i + 8]
            byte_str = ",".join(f"0x{b:02X}" for b in part)
            lines_out.append(f"  {byte_str},")
        lines_out.append("};")
        lines_out.append("")
        return "\n".join(lines_out)

    def _format_c_array_status(self, status_data):
        out_lines = []
        out_lines.append("// Status Memory (256 Bytes)")
        out_lines.append("const uint8_t status_mem[256] PROGMEM = {")
        for base in range(0, 256, 16):
            chunk = status_data[base:base + 16]
            byte_str = ",".join(f"0x{b:02X}" for b in chunk)
            out_lines.append(f"  // 0x{base:04X}")
            out_lines.append(f"  {byte_str},")
        out_lines.append("};")
        out_lines.append("")
        return "\n".join(out_lines)

    def generate_ds2506_header(self, rom_info, binary_data, status_data, filename="ds2506_image.h"):
        if not binary_data or len(binary_data) != 8192:
            print("generate_ds2506_header: binary_data fehlt oder hat nicht 8192 Bytes.")
            return None
        if not status_data or len(status_data) != 256:
            print("generate_ds2506_header: status_data fehlt oder hat nicht 256 Bytes.")
            return None

        PAGE_SIZE = 32
        total_pages = len(binary_data) // PAGE_SIZE

        header_lines = []
        header_lines.append("// AUTOMATISCH GENERIERT von read_ds2506.py")
        header_lines.append("// Datei für den Emulator (DS2506_Custom).")
        header_lines.append("// WARNUNG: Manuelle Änderungen werden beim nächsten Export überschrieben.\n")
        header_lines.append("#pragma once")
        header_lines.append("#include <Arduino.h>")
        header_lines.append("#include <avr/pgmspace.h>\n")

        # ROM Info Kommentar (wenn verfügbar)
        rb = None
        if rom_info and "rom_bytes" in rom_info:
            rb = rom_info["rom_bytes"]
        if rb and len(rb) == 8:
            rom_hex = " ".join(f"{b:02X}" for b in rb)
            header_lines.append(f"// ROM Code: {rom_hex}")
            header_lines.append(f"// Family Code: 0x{rb[0]:02X}")
            header_lines.append(f"// CRC Chip:    0x{rb[7]:02X}")
            calc_crc = self.compute_crc8_maxim(rb[:7])
            header_lines.append(
                f"// CRC Calc:    0x{calc_crc:02X} "
                + ("(OK)" if calc_crc == rb[7] else "(MISMATCH)")
            )
            header_lines.append("")

        # belegte Pages exportieren
        used_pages = []
        for page_index in range(total_pages):
            start = page_index * PAGE_SIZE
            end = start + PAGE_SIZE
            chunk = binary_data[start:end]

            if any(b != 0xFF for b in chunk):
                used_pages.append(page_index)
                array_name = f"page_{start:04X}"
                arr_txt = self._format_c_array_page(array_name, chunk, start)
                header_lines.append(arr_txt)

        # Status anhängen
        status_txt = self._format_c_array_status(status_data)
        header_lines.append(status_txt)

        with open(filename, "w", encoding="utf-8") as f:
            f.write("\n".join(header_lines))

        print(f"✓ Header-Datei '{filename}' erzeugt.")
        print(f"  Enthält {len(used_pages)} belegte Pages von {total_pages} insgesamt.")
        if used_pages:
            print("  Arrays: " + ", ".join(f"page_{p*PAGE_SIZE:04X}" for p in used_pages) + ", status_mem")
        else:
            print("  Arrays: status_mem (keine belegten Pages gefunden?)")

        return filename


# -------------------------------------------------
def interactive_mode(reader):
    print("\n=== Interaktiver Modus ===")
    print("Kommandos (gehen direkt an den Arduino):")
    print("  all         - Gesamtspeicher anzeigen (Arduino-Ausgabe)")
    print("  hexdump     - Klassischer Dump (Arduino)")
    print("  binary      - Binärdaten anzeigen (Arduino-Ausgabe)")
    print("  status      - Status Memory anzeigen (Arduino-Ausgabe)")
    print("  rom         - ROM Code anzeigen (Arduino-Ausgabe)")
    print("  help        - Arduino-Hilfe")
    print("  pages       - (falls Arduino das kennt) belegte Pages listen")
    print("  [adresse]   - Speicher ab Adresse (hex)")
    print("")
    print("Kommandos (Python-Auswertung):")
    print("  savebin     - 8KB holen, binary.bin schreiben")
    print("  savehex     - 8KB holen, hexdump.hex schreiben")
    print("  savestatus  - 256B holen, status.bin schreiben + Analyse")
    print("  savefull    - Alles holen, dump_report.txt schreiben")
    print("  pypages     - belegte Pages anzeigen")
    print("  makeds2506  - Header ds2506_image.h schreiben")
    print("  saveall     - ALLES holen und ALLE Dateien mit Präfix schreiben")
    print("")
    print("  quit/exit   - Beenden")
    print("")

    while True:
        try:
            cmd = input("DS2506> ").strip()
            if not cmd:
                continue

            cl = cmd.lower()
            if cl in ["quit", "exit", "q"]:
                break

            if cl == "savebin":
                data = reader.read_binary_data()
                if data:
                    reader.save_binary(data)

            elif cl == "savehex":
                data = reader.read_binary_data()
                if data:
                    reader.save_hexdump(data)

            elif cl == "savestatus":
                status = reader.read_status_data()
                if status:
                    reader.save_status(status)
                    reader.analyze_status(status)

            elif cl == "pypages":
                data = reader.read_binary_data()
                if data:
                    reader.calc_used_pages_from_binary(data)

            elif cl == "makeds2506":
                print("\n=== ds2506_image.h Generator ===")
                rominfo = reader.get_rom_info()
                data = reader.read_binary_data()
                status = reader.read_status_data()
                if data and status:
                    reader.generate_ds2506_header(
                        rom_info=rominfo,
                        binary_data=data,
                        status_data=status,
                        filename="ds2506_image.h",
                    )

            elif cl == "savefull":
                print("\n=== Kompletter Backup ===")
                rominfo = reader.get_rom_info()

                data = reader.read_binary_data()
                used_pages = None
                page_hexdump_map = None
                if data:
                    reader.save_binary(data, "binary.bin")
                    reader.save_hexdump(data, "hexdump.hex")
                    used_pages, page_hexdump_map = reader.calc_used_pages_from_binary(data)

                status = reader.read_status_data()
                status_analysis_text = ""
                if status:
                    reader.save_status(status, "status.bin")
                    status_analysis_text = reader.analyze_status(status)

                reader.save_full_report(
                    rom_info=rominfo,
                    binary_data=data,
                    status_data=status,
                    status_analysis_text=status_analysis_text,
                    used_pages=used_pages,
                    page_hexdump_map=page_hexdump_map,
                    filename="dump_report.txt",
                )

                print("\n✓ Kompletter Backup abgeschlossen!")

            elif cl == "saveall":
                print("\n=== Gesamtexport mit Präfix ===")
                user_tag = input("Bitte Kennstring (String1) eingeben: ").strip()

                # 1. alles holen
                rominfo = reader.get_rom_info()
                data = reader.read_binary_data()
                status = reader.read_status_data()

                if not data or len(data) != 8192:
                    print("Abbruch: 8KB Dump ungültig oder unvollständig.")
                    continue
                if not status or len(status) != 256:
                    print("Abbruch: Statusdump ungültig oder unvollständig.")
                    continue

                used_pages, page_hexdump_map = reader.calc_used_pages_from_binary(data)
                status_analysis_text = reader.analyze_status(status)

                # 2. Präfix bauen
                prefix = reader.build_prefix(data, user_tag)

                # 3. Dateinamen erzeugen
                fname_bin = f"{prefix}_binary.bin"
                fname_hex = f"{prefix}_hexdump.hex"
                fname_stat = f"{prefix}_status.bin"
                fname_rep = f"{prefix}_dump_report.txt"
                fname_hdr = f"{prefix}_ds2506_image.h"

                # 4. Dateien schreiben
                reader.save_binary(data, fname_bin)
                reader.save_hexdump(data, fname_hex)
                reader.save_status(status, fname_stat)

                reader.save_full_report(
                    rom_info=rominfo,
                    binary_data=data,
                    status_data=status,
                    status_analysis_text=status_analysis_text,
                    used_pages=used_pages,
                    page_hexdump_map=page_hexdump_map,
                    filename=fname_rep,
                )

                reader.generate_ds2506_header(
                    rom_info=rominfo,
                    binary_data=data,
                    status_data=status,
                    filename=fname_hdr,
                )

                print("\n✓ Alle Dateien erzeugt.")

            else:
                # Unbekanntes Kommando -> direkt weiterleiten an Arduino
                reader.send_command(cmd)

        except KeyboardInterrupt:
            print("\n\nAbgebrochen")
            break
        except Exception as e:
            print(f"Fehler: {e}")


# -------------------------------------------------
def main():
    if len(sys.argv) < 2:
        print("Nutzung:")
        print("  python read_ds2506_final.py COM7")
        print("  python read_ds2506_final.py /dev/ttyUSB0")
        print()
        print("DS2506/DS2433 Reader (8KB)")
        sys.exit(1)

    port = sys.argv[1]
    reader = DS2506Reader(port)

    if not reader.connect():
        sys.exit(1)

    try:
        interactive_mode(reader)
    finally:
        reader.disconnect()


if __name__ == "__main__":
    main()
