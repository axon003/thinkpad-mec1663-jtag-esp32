#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
memprog.py - host pentru ESP32 Multi-Memory Programmer (firmware esp32_memprog)
Versiune: 1.0

Vorbeste cu ESP32 pe USB Serial (protocol linie-cu-linie descris in README) si
ofera subcomenzi practice:

  id                    - PING + SPIID + MECID (ce vede firmware-ul)
  spi-id                - JEDEC ID al cipului SPI NOR
  spi-dump              - citeste tot SPI NOR intr-un .bin
  spi-write             - scrie un .bin in SPI NOR (erase + program + verify)
  mec-id                - IDCODE MEC/ARC (verifica ARC6xx)
  mec-status            - security status (Boot_Block/Data_Block/EEPROM_Block)
  mec-dump              - citeste flash (+ optional eeprom) MEC intr-un .bin
  mec-surgical-clear    - stergere CHIRURGICALA: pastreaza serial/MAC/DMI, sterge
                          DOAR zona [offset..offset+len), reprograme paginile
                          afectate din backup, verifica prin re-read.
  mec-mass-erase        - emergency mass erase (STERGE serial/MAC! confirmare)

FARA erori silent: orice ERR de la firmware sau mismatch de verify opreste cu exceptie.
"""

import argparse
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("Lipseste pyserial. Instaleaza: pip install pyserial")


PAGE_SIZE = 2048          # granularitate erase MEC flash
EEPROM_SIZE = 2048
FLASH_SIZE_MAX = 0x40000  # 256 KiB (MEC1663)


class MemProgError(Exception):
    pass


class _TcpSerial:
    """Minimal interfata de tip pyserial peste un socket TCP (shell raw ergProgrammer, port 2323)."""

    def __init__(self, hostport, timeout):
        import socket
        host, _, prt = hostport.partition(":")
        try:
            self.sock = socket.create_connection((host, int(prt or 2323)), timeout=timeout)
        except OSError as e:
            raise MemProgError("nu ma pot conecta la %s: %s" % (hostport, e))
        self.sock.settimeout(timeout)
        self.buf = bytearray()
        self.readline()   # banner "# ergProgrammer ..."

    def write(self, data):
        self.sock.sendall(data)

    def flush(self):
        pass

    def reset_input_buffer(self):
        self.buf.clear()

    def readline(self):
        import socket
        while b"\n" not in self.buf:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                return b""
            if not chunk:
                return b""
            self.buf.extend(chunk)
        i = self.buf.index(b"\n") + 1
        line = bytes(self.buf[:i])
        del self.buf[:i]
        return line

    def close(self):
        self.sock.close()


class Link:
    """Legatura serial cu firmware-ul. read_until_status() aduna liniile 'D <hex>'
    si se opreste la OK/ERR (ridica exceptie pe ERR - fara inghitit tacut)."""

    def __init__(self, port, baud=921600, timeout=10):
        # FW 2.0: port poate fi "tcp:host[:port]" (shell raw pe 2323) in loc de COMx
        if port.lower().startswith("tcp:"):
            self.ser = _TcpSerial(port[4:], timeout)
        else:
            self.ser = serial.Serial(port, baud, timeout=timeout)
            time.sleep(0.3)
            self.ser.reset_input_buffer()

    def close(self):
        self.ser.close()

    def send(self, line):
        self.ser.write((line + "\n").encode("ascii"))
        self.ser.flush()

    def read_until_status(self, quiet=False):
        """Intoarce (data_bytes, ok_message). Ridica MemProgError pe ERR/timeout."""
        data = bytearray()
        msg = ""
        while True:
            raw = self.ser.readline()
            if not raw:
                raise MemProgError("timeout la citirea raspunsului de la ESP32")
            s = raw.decode("ascii", "replace").rstrip("\r\n")
            if s.startswith("D "):
                data.extend(bytes.fromhex(s[2:]))
            elif s.startswith("# "):
                if not quiet:
                    print("  " + s[2:])
            elif s == "OK" or s.startswith("OK "):
                msg = s[3:] if s.startswith("OK ") else ""
                break
            elif s.startswith("ERR "):
                raise MemProgError(s[4:])
            elif s == "":
                continue
            else:
                # linie neasteptata - o afisam, dar nu o inghitim tacut
                if not quiet:
                    print("  ?? " + s)
        return bytes(data), msg

    def cmd(self, line, quiet=False):
        self.send(line)
        return self.read_until_status(quiet=quiet)


def _hx(b):
    return b.hex()


# --------------------------------------------------------------------------
# Subcomenzi
# --------------------------------------------------------------------------
def cmd_id(link, args):
    _, m = link.cmd("PING")
    print("PING:", m)
    try:
        _, m = link.cmd("SPIID")
        print("SPI NOR:", m)
    except MemProgError as e:
        print("SPI NOR: (indisponibil)", e)
    try:
        _, m = link.cmd("MECID")
        print("MEC/ARC:", m)
    except MemProgError as e:
        print("MEC/ARC:", e)


def cmd_spi_id(link, args):
    _, m = link.cmd("SPIID")
    print(m)


def cmd_spi_dump(link, args):
    if args.four_byte:
        link.cmd("SPI4B on")
    size = args.size
    chunk = args.chunk
    out = bytearray()
    t0 = time.time()
    for off in range(0, size, chunk):
        n = min(chunk, size - off)
        data, _ = link.cmd("SPIRD %x %x" % (off, n), quiet=True)
        if len(data) != n:
            raise MemProgError("SPIRD a intors %d bytes, asteptam %d @0x%x" % (len(data), n, off))
        out.extend(data)
        print("\r  citit 0x%06x / 0x%06x" % (off + n, size), end="", flush=True)
    print("")
    with open(args.file, "wb") as f:
        f.write(out)
    print("Salvat %d bytes in %s (%.1fs)" % (len(out), args.file, time.time() - t0))


def cmd_spi_write(link, args):
    with open(args.file, "rb") as f:
        data = f.read()
    if args.four_byte:
        link.cmd("SPI4B on")
    if args.erase:
        print("Chip erase...")
        link.cmd("SPIERASE chip")
    # programare pe pagini (default 256)
    page = 256
    for off in range(0, len(data), page):
        blk = data[off:off + page]
        link.cmd("SPIWR %x %s" % (off, _hx(blk)), quiet=True)
        print("\r  scris 0x%06x / 0x%06x" % (off + len(blk), len(data)), end="", flush=True)
    print("")
    if args.verify:
        print("Verific prin re-read...")
        for off in range(0, len(data), args.chunk):
            n = min(args.chunk, len(data) - off)
            rd, _ = link.cmd("SPIRD %x %x" % (off, n), quiet=True)
            if rd != data[off:off + n]:
                raise MemProgError("VERIFY MISMATCH @0x%x" % off)
        print("Verify OK")


def cmd_mec_id(link, args):
    _, m = link.cmd("MECID")
    print(m)


def cmd_mec_status(link, args):
    _, m = link.cmd("MECSTATUS")
    print(m)


def _mec_read_flash(link, size, words_per_cmd=4096):
    """Citeste 'size' bytes de flash MEC (aliniat la 4). Intoarce bytes."""
    total_words = (size + 3) // 4
    out = bytearray()
    got = 0
    t0 = time.time()
    while got < total_words:
        n = min(words_per_cmd, total_words - got)
        data, _ = link.cmd("MECRD %x %x" % (got * 4, n), quiet=True)
        if len(data) != n * 4:
            raise MemProgError("MECRD a intors %d bytes, asteptam %d @word 0x%x"
                               % (len(data), n * 4, got))
        out.extend(data)
        got += n
        print("\r  citit 0x%06x / 0x%06x  (%.0fs)" % (got * 4, total_words * 4, time.time() - t0),
              end="", flush=True)
    print("")
    return bytes(out[:size])


def cmd_mec_dump(link, args):
    # verifica ARC6xx intai
    _, m = link.cmd("MECID")
    print("MEC/ARC:", m)
    if "ARC6xx" not in m:
        raise MemProgError("nu e ARC6xx - opresc (foloseste --force-not-arc daca chiar vrei)"
                           if not args.force_not_arc else m)
    print("Citesc flash %d bytes..." % args.size)
    flash = _mec_read_flash(link, args.size, args.words_per_cmd)
    with open(args.file, "wb") as f:
        f.write(flash)
    print("Salvat flash: %s (%d bytes)" % (args.file, len(flash)))
    if args.eeprom:
        print("Citesc eeprom %d bytes..." % EEPROM_SIZE)
        ee, _ = link.cmd("MECEERD", quiet=True)
        if len(ee) != EEPROM_SIZE:
            raise MemProgError("MECEERD a intors %d bytes, asteptam %d" % (len(ee), EEPROM_SIZE))
        with open(args.eeprom, "wb") as f:
            f.write(ee)
        print("Salvat eeprom: %s (%d bytes)" % (args.eeprom, len(ee)))


def cmd_mec_surgical_clear(link, args):
    """Stergere chirurgicala:
       1. citeste backup INTEGRAL flash (daca nu e dat --backup existent)
       2. construieste imaginea noua: identica cu backup, dar zona
          [offset..offset+len) setata la fill (0xFF implicit)
       3. sterge DOAR paginile (2048B) care ating zona tinta
       4. reprogrameaza acele pagini din imaginea noua (serial/MAC din aceeasi
          pagina se pastreaza pentru ca provin din backup)
       5. verifica prin re-read al paginilor scrise
    """
    offset = args.offset
    length = args.len
    fill = args.fill & 0xFF

    if offset % 4 != 0 or length % 4 != 0:
        raise MemProgError("offset si len trebuie multiplu de 4 (words de flash)")
    if offset + length > FLASH_SIZE_MAX:
        raise MemProgError("zona depaseste 0x%x" % FLASH_SIZE_MAX)

    # 1. backup
    _, m = link.cmd("MECID")
    print("MEC/ARC:", m)
    if "ARC6xx" not in m and not args.force_not_arc:
        raise MemProgError("nu e ARC6xx - opresc")

    if args.backup and _file_exists(args.backup) and not args.reread:
        with open(args.backup, "rb") as f:
            image = bytearray(f.read())
        print("Folosesc backup existent: %s (%d bytes)" % (args.backup, len(image)))
    else:
        print("Citesc backup INTEGRAL flash %d bytes..." % args.size)
        image = bytearray(_mec_read_flash(link, args.size, args.words_per_cmd))
        bpath = args.backup or (args.image_prefix + "_flash_backup.bin")
        with open(bpath, "wb") as f:
            f.write(image)
        print("Backup salvat: %s" % bpath)

    if offset + length > len(image):
        raise MemProgError("zona tinta depaseste dimensiunea backup-ului (%d)" % len(image))

    # salveaza si eeprom-ul ca plasa de siguranta
    if args.eeprom_backup:
        try:
            ee, _ = link.cmd("MECEERD", quiet=True)
            with open(args.eeprom_backup, "wb") as f:
                f.write(ee)
            print("Backup eeprom salvat: %s (%d bytes)" % (args.eeprom_backup, len(ee)))
        except MemProgError as e:
            print("AVERTISMENT: nu am putut salva eeprom-ul:", e)

    # 2. imaginea noua
    old_region = bytes(image[offset:offset + length])
    new_image = bytearray(image)
    for i in range(offset, offset + length):
        new_image[i] = fill
    print("Zona tinta 0x%05x..0x%05x (%d bytes) -> 0x%02X" %
          (offset, offset + length, length, fill))
    print("  inainte:", old_region[:32].hex(), "..." if length > 32 else "")

    # 3+4. pagini afectate
    first_page = (offset // PAGE_SIZE) * PAGE_SIZE
    last_page = ((offset + length - 1) // PAGE_SIZE) * PAGE_SIZE
    pages = list(range(first_page, last_page + 1, PAGE_SIZE))
    print("Pagini afectate (%d): %s" % (len(pages), ", ".join("0x%05x" % p for p in pages)))

    if args.dry_run:
        print("[DRY-RUN] Nu se scrie nimic. Verifica offset/len de mai sus.")
        return

    if not args.yes:
        raise MemProgError("adauga --yes pentru a executa erase+program (sau --dry-run)")

    for p in pages:
        print("Pagina 0x%05x: erase..." % p)
        link.cmd("MECERASEPG %x" % p)
        # reprogram pagina din imaginea noua, in chunk-uri
        seg = new_image[p:p + PAGE_SIZE]
        # pad la multiplu de 4
        if len(seg) % 4:
            seg = seg + bytes((4 - len(seg) % 4))
        cw = args.words_per_cmd * 4
        for o in range(0, len(seg), cw):
            blk = seg[o:o + cw]
            link.cmd("MECPROG %x %s" % (p + o, _hx(bytes(blk))), quiet=True)
        print("  program OK")

    # 5. verify prin re-read al paginilor scrise
    print("Verific prin re-read...")
    for p in pages:
        expect = bytes(new_image[p:p + PAGE_SIZE])
        rd = _mec_read_flash_region(link, p, len(expect), args.words_per_cmd)
        if rd != expect:
            # gaseste primul byte diferit pentru raport
            for i in range(len(expect)):
                if rd[i] != expect[i]:
                    raise MemProgError("VERIFY MISMATCH @0x%05x: citit 0x%02X, asteptat 0x%02X"
                                       % (p + i, rd[i], expect[i]))
            raise MemProgError("VERIFY MISMATCH pe pagina 0x%05x (lungime)" % p)
    print("Verify OK. Stergere chirurgicala completa.")
    print("Serial/MAC/DMI din paginile afectate au fost pastrate (din backup).")


def _mec_read_flash_region(link, addr, size, words_per_cmd):
    total_words = (size + 3) // 4
    out = bytearray()
    got = 0
    while got < total_words:
        n = min(words_per_cmd, total_words - got)
        data, _ = link.cmd("MECRD %x %x" % (addr + got * 4, n), quiet=True)
        if len(data) != n * 4:
            raise MemProgError("MECRD region: %d bytes, asteptam %d" % (len(data), n * 4))
        out.extend(data)
        got += n
    return bytes(out[:size])


def cmd_mec_mass_erase(link, args):
    if not args.yes:
        raise MemProgError("mec-mass-erase STERGE serial/MAC/DMI. Confirma cu --yes")
    print("EMERGENCY MASS ERASE - sterge TOT (flash+eeprom, inclusiv serial/MAC)...")
    _, m = link.cmd("MECMASSERASE CONFIRM")
    print(m)


def _file_exists(p):
    import os
    return os.path.isfile(p)


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="Host ESP32 Multi-Memory Programmer")
    ap.add_argument("-p", "--port", required=True, help="port serial (COM7, /dev/ttyUSB0) sau tcp:<ip>[:2323] pentru shell-ul din retea")
    ap.add_argument("-b", "--baud", type=int, default=921600)
    sub = ap.add_subparsers(dest="op", required=True)

    sub.add_parser("id")
    sub.add_parser("spi-id")

    p = sub.add_parser("spi-dump")
    p.add_argument("file")
    p.add_argument("-s", "--size", type=lambda x: int(x, 0), required=True, help="bytes de citit")
    p.add_argument("--chunk", type=lambda x: int(x, 0), default=4096)
    p.add_argument("--four-byte", action="store_true", help="adresare 4-byte (>16MB)")

    p = sub.add_parser("spi-write")
    p.add_argument("file")
    p.add_argument("--erase", action="store_true", help="chip erase inainte")
    p.add_argument("--verify", action="store_true")
    p.add_argument("--chunk", type=lambda x: int(x, 0), default=4096)
    p.add_argument("--four-byte", action="store_true")

    sub.add_parser("mec-id")
    sub.add_parser("mec-status")

    p = sub.add_parser("mec-dump")
    p.add_argument("file", help="fisier .bin flash")
    p.add_argument("-s", "--size", type=lambda x: int(x, 0), default=FLASH_SIZE_MAX,
                   help="dimensiune flash (implicit 256K). Recomandat 256K pt a nu pierde date.")
    p.add_argument("--eeprom", help="salveaza si eeprom-ul in acest fisier")
    p.add_argument("--words-per-cmd", type=lambda x: int(x, 0), default=4096)
    p.add_argument("--force-not-arc", action="store_true")

    p = sub.add_parser("mec-surgical-clear")
    p.add_argument("--offset", type=lambda x: int(x, 0), required=True,
                   help="offset (bytes) al zonei tinta - vezi README cum se afla")
    p.add_argument("--len", type=lambda x: int(x, 0), required=True, help="lungime zona (bytes)")
    p.add_argument("--fill", type=lambda x: int(x, 0), default=0xFF, help="octet de umplere (0xFF implicit)")
    p.add_argument("--size", type=lambda x: int(x, 0), default=FLASH_SIZE_MAX, help="dimensiune flash pt backup")
    p.add_argument("--backup", help="fisier backup flash (se creeaza daca lipseste)")
    p.add_argument("--eeprom-backup", help="salveaza si eeprom ca plasa de siguranta")
    p.add_argument("--image-prefix", default="mec", help="prefix pt fisiere generate")
    p.add_argument("--words-per-cmd", type=lambda x: int(x, 0), default=4096)
    p.add_argument("--reread", action="store_true", help="reciteste backup chiar daca fisierul exista")
    p.add_argument("--dry-run", action="store_true", help="doar arata paginile afectate, nu scrie")
    p.add_argument("--yes", action="store_true", help="confirma erase+program")
    p.add_argument("--force-not-arc", action="store_true")

    p = sub.add_parser("mec-mass-erase")
    p.add_argument("--yes", action="store_true")

    args = ap.parse_args()
    link = Link(args.port, args.baud)
    try:
        dispatch = {
            "id": cmd_id,
            "spi-id": cmd_spi_id,
            "spi-dump": cmd_spi_dump,
            "spi-write": cmd_spi_write,
            "mec-id": cmd_mec_id,
            "mec-status": cmd_mec_status,
            "mec-dump": cmd_mec_dump,
            "mec-surgical-clear": cmd_mec_surgical_clear,
            "mec-mass-erase": cmd_mec_mass_erase,
        }
        dispatch[args.op](link, args)
    except MemProgError as e:
        sys.exit("EROARE: %s" % e)
    finally:
        link.close()


if __name__ == "__main__":
    main()
