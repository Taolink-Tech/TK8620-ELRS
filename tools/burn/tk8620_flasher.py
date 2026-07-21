#!/usr/bin/env python3
r"""TK8620 UART flasher.

This implements the TK8620 serial flashing protocol and uses the public
patch image shipped in this repository.
"""

from __future__ import annotations

import argparse
import binascii
import ctypes
import ctypes.wintypes
import re
import struct
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - depends on host Python
    raise SystemExit("pyserial is required. Install it with: pip install pyserial") from exc


OPCODE_VERSION_GET = 0x00
OPCODE_VERSION_GET_ACK = 0x01
OPCODE_WRITE = 0x02
OPCODE_WRITE_ACK = 0x03
OPCODE_WRITE_RAM = 0x04
OPCODE_WRITE_RAM_ACK = 0x05
OPCODE_READ = 0x08
OPCODE_READ_ACK = 0x09
OPCODE_ERASE = 0x0C
OPCODE_ERASE_ACK = 0x0D
OPCODE_DISCONNECT = 0x10
OPCODE_CHANGE_BAUDRATE = 0x12
OPCODE_CHANGE_BAUDRATE_ACK = 0x13
OPCODE_EXECUTE_CODE = 0x15
OPCODE_EXECUTE_CODE_END = 0x17
OPCODE_CALC_CRC32 = 0x19
OPCODE_CALC_CRC32_ACK = 0x1A
OPCODE_BLOCK64K_ERASE = 0x1D
OPCODE_BLOCK64K_ERASE_ACK = 0x1E

BOOTPATCH_BASE = 0x20080400
BOOTPATCH_CHUNK = 0x200

FLASH_IMAGE_SIZE = 0x30000
FLASH_ERASE_END = 0x40000
FLASH_WRITE_CHUNK = 0x100
ELRS_USER_PARAM_START = 0x69000
ELRS_USER_PARAM_END = 0x7F000
# Keep clear of SDK PHY backup sectors at 0x7E000 and 0x7F000.
RX_OTA_META_OFFSET = 0x7D000
FLASH_SECTOR_SIZE = 0x1000
BOOTLOADER_LIMIT = 0x0D800
APP_OFFSET = 0x0D800
APP_LIMIT = 0x22800 - 8
RX_STAGE_IMAGE_OFFSET = 0x41000
RX_STAGE_IMAGE_SIZE = 0x22800
RX_STAGE_RESERVED_OFFSET = 0x63800
QSPI_PARAM_OFFSET = 0x2FFF8
CHECK_WORD_OFFSET = 0x2FFFC
QSPI_PARAM = 0x00230401
CHECK_WORD = 0x51525251
XIP_FLASH_BASE = 0xC2000000
FLASH_READ_CHUNK = 0x800
VERBOSE = False
RX_OTA_META_MAGIC = 0x41544F52  # "ROTA", little-endian
RX_OTA_META_VERSION = 1
RX_OTA_META_SIZE = 40
RX_OTA_DEFAULT_FREQ = 900320000
SERIAL_RESET_LOW_MS = 50
SERIAL_RESET_SETTLE_MS = 20
AUTO_RESET_HANDSHAKE_TIMEOUT_S = 0.2


def log(message: str) -> None:
    print(message, flush=True)


def progress(label: str, done: int, total: int, *, force: bool = False) -> None:
    if VERBOSE and not force:
        return

    width = 28
    if total <= 0:
        percent = 100
    else:
        percent = min(100, int(done * 100 / total))
    filled = width * percent // 100
    bar = "#" * filled + "-" * (width - filled)
    print(f"\r{label} [{bar}] {percent:3d}%", end="", flush=True)
    if percent >= 100:
        print(flush=True)


def status(message: str) -> None:
    if not VERBOSE:
        log(message)


def debug(message: str) -> None:
    if VERBOSE:
        log(message)


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def parse_version_text(value: str) -> tuple[int, int, int]:
    match = re.match(r"^(\d+)\.(\d+)\.(\d+)", value.strip())
    if not match:
        raise ValueError(f"invalid Rx version '{value}', expected MAJOR.MINOR.REVISION")

    parts = tuple(int(part) for part in match.groups())
    if any(part > 255 for part in parts):
        raise ValueError(f"invalid Rx version '{value}', each part must fit in one byte")
    return parts


def infer_version_from_firmware(firmware: bytes):
    match = re.search(rb"(?<![0-9A-Za-z])(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?:-[0-9A-Za-z.]+)?", firmware)
    if not match:
        return None

    parts = tuple(int(part.decode("ascii")) for part in match.groups())
    if any(part > 255 for part in parts):
        return None
    return parts


def resolve_rx_version(value, root: Path, firmware: bytes):
    if value:
        return parse_version_text(value), "command line"

    inferred = infer_version_from_firmware(firmware)
    if inferred is not None:
        return inferred, "Rx firmware image"

    version_file = root / "VERSION"
    fallback = version_file.read_text(encoding="utf-8").strip().splitlines()[0]
    return parse_version_text(fallback), str(version_file)


def parse_target_id(value) -> bytes:
    if not value:
        return bytes([0xFF] * 8)

    text = value.replace(":", "").replace("-", "").strip()
    if not re.fullmatch(r"[0-9A-Fa-f]{16}", text):
        raise ValueError("--target-id must be 8 bytes, e.g. FFFFFFFFFFFFFFFF")
    return bytes.fromhex(text)


def build_rx_ota_meta(
    *,
    firmware_len: int,
    firmware_crc: int,
    image_offset: int,
    version: tuple[int, int, int],
    freq: int,
    target_id: bytes,
) -> bytes:
    header = struct.pack(
        "<IHHIIII4s8s",
        RX_OTA_META_MAGIC,
        RX_OTA_META_VERSION,
        RX_OTA_META_SIZE,
        image_offset,
        firmware_len,
        firmware_crc,
        freq,
        bytes([version[0], version[1], version[2], 0]),
        target_id,
    )
    meta_crc = crc16_modbus(header)
    return header + struct.pack("<HH", meta_crc, 0xFFFF)


def pack_header(code: int, address: int, length: int) -> bytes:
    return struct.pack("<BIH", code & 0xFF, address & 0xFFFFFFFF, length & 0xFFFF)


def unpack_header(data: bytes) -> tuple[int, int, int]:
    if len(data) < 7:
        raise ValueError(f"short response: {data.hex(' ')}")
    return struct.unpack("<BIH", data[:7])


def parse_strhex(path: Path) -> bytes:
    words: list[int] = []
    with path.open("r", encoding="ascii", errors="strict") as f:
        for line_no, line in enumerate(f, 1):
            text = line.strip()
            if not text:
                continue
            if not re.fullmatch(r"[0-9A-Fa-f]{8}", text):
                raise ValueError(f"{path}:{line_no}: expected 8 hex characters, got {text!r}")
            words.append(int(text, 16))

    data = bytearray()
    for word in words:
        data.extend(struct.pack("<I", word))
    return bytes(data)


def parse_patch_8620(header_path: Path) -> bytes:
    text = header_path.read_text(encoding="ascii", errors="ignore")
    match = re.search(r"const\s+unsigned\s+int\s+patch_8620\s*\[\]\s*=\s*\{(?P<body>.*?)\};", text, re.S)
    if not match:
        raise ValueError(f"patch_8620[] was not found in {header_path}")

    values = [int(value, 16) for value in re.findall(r"0x[0-9A-Fa-f]+", match.group("body"))]
    data = bytearray()
    for value in values:
        data.extend(struct.pack("<I", value & 0xFFFFFFFF))

    if not data:
        raise ValueError(f"patch_8620 is empty in {header_path}")
    return bytes(data)


def build_local_image(kind: str, bootloader_path: Path, firmware_path: Path) -> bytes:
    bootloader = parse_strhex(bootloader_path)
    firmware = parse_strhex(firmware_path)

    if len(bootloader) > BOOTLOADER_LIMIT:
        raise ValueError(
            f"bootloader is too large: 0x{len(bootloader):X}, limit 0x{BOOTLOADER_LIMIT:X}"
        )
    if len(firmware) > APP_LIMIT:
        raise ValueError(f"{kind} firmware is too large: 0x{len(firmware):X}, limit 0x{APP_LIMIT:X}")

    image = bytearray([0xFF] * FLASH_IMAGE_SIZE)
    image[0 : len(bootloader)] = bootloader
    image[APP_OFFSET : APP_OFFSET + len(firmware)] = firmware
    struct.pack_into("<I", image, QSPI_PARAM_OFFSET, QSPI_PARAM)
    struct.pack_into("<I", image, CHECK_WORD_OFFSET, CHECK_WORD)
    return bytes(image)


def sector_range(start: int, length: int) -> range:
    first = start - (start % FLASH_SECTOR_SIZE)
    last = start + length
    return range(first, last, FLASH_SECTOR_SIZE)


def sector_erase_end(start: int, length: int) -> int:
    addresses = list(sector_range(start, length))
    if not addresses:
        return start
    return addresses[-1] + FLASH_SECTOR_SIZE


class Tk8620Bootrom:
    def __init__(self, port: str, baud: int) -> None:
        self.port = port
        self.target_baud = baud
        self.ser = serial.Serial(
            port=port,
            baudrate=115200,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0,
            write_timeout=5,
            rtscts=False,
            dsrdtr=False,
        )

    def close(self, rts_active: bool | None = None) -> None:
        try:
            if rts_active is not None:
                self.ser.rts = rts_active
        finally:
            self.ser.close()

    def write(self, payload: bytes) -> None:
        self.ser.write(payload)
        self.ser.flush()

    def serial_port_handle(self) -> ctypes.wintypes.HANDLE:
        """Return the already-open Windows COM handle for reset control."""
        handle = getattr(self.ser, "_port_handle", None)
        if handle is None:
            raise OSError("serial port handle is not available.")
        if hasattr(handle, "value"):
            value = handle.value
        else:
            value = int(handle)
        if value is None or value == 0:
            raise OSError("serial port handle is invalid.")
        return ctypes.wintypes.HANDLE(value)

    def set_rts_control_dcb(self, active: bool) -> None:
        import serial.win32 as win32

        dcb = win32.DCB()
        handle = self.serial_port_handle()
        if not win32.GetCommState(handle, ctypes.byref(dcb)):
            raise OSError(ctypes.get_last_error(), "GetCommState failed")
        dcb.fOutxCtsFlow = 0
        dcb.fRtsControl = win32.RTS_CONTROL_ENABLE if active else win32.RTS_CONTROL_DISABLE
        if not win32.SetCommState(handle, ctypes.byref(dcb)):
            raise OSError(ctypes.get_last_error(), "SetCommState failed")

    def pulse_serial_reset(self, low_ms: int = SERIAL_RESET_LOW_MS, settle_ms: int = SERIAL_RESET_SETTLE_MS) -> None:
        self.set_rts_control_dcb(True)
        time.sleep(low_ms / 1000.0)
        self.set_rts_control_dcb(False)
        if settle_ms > 0:
            time.sleep(settle_ms / 1000.0)

    def read_until_contains(self, needle: bytes, timeout_s: float) -> bytes:
        deadline = time.monotonic() + timeout_s
        buf = bytearray()
        while time.monotonic() < deadline:
            chunk = self.ser.read(256)
            if chunk:
                buf.extend(chunk)
                if needle in buf:
                    return bytes(buf)
            else:
                time.sleep(0.001)
        raise TimeoutError(f"timeout waiting for {needle!r}; received {bytes(buf)!r}")

    def read_packet(self, timeout_s: float = 3.0, min_len: int = 7) -> bytes:
        deadline = time.monotonic() + timeout_s
        buf = bytearray()
        while time.monotonic() < deadline:
            chunk = self.ser.read(min_len - len(buf))
            if chunk:
                buf.extend(chunk)
                if len(buf) >= min_len:
                    time.sleep(0.01)
                    waiting = self.ser.in_waiting
                    if waiting:
                        buf.extend(self.ser.read(waiting))
                    return bytes(buf)
            else:
                time.sleep(0.001)
        raise TimeoutError(f"timeout waiting for response; received {bytes(buf).hex(' ')}")

    def bootrom_handshake_once(self, timeout_s: float | None) -> bytes:
        deadline = None if timeout_s is None else time.monotonic() + timeout_s
        buf = bytearray()
        sent_taolink = False
        last_turmass_at = 0.0

        while deadline is None or time.monotonic() < deadline:
            chunk = self.ser.read(256)
            if not chunk:
                time.sleep(0.001)
                continue

            buf.extend(chunk)

            if b"ok" in buf:
                return bytes(buf)

            if b"TurMass." in buf:
                now = time.monotonic()
                if not sent_taolink or now - last_turmass_at > 0.05:
                    self.write(b"TaoLink.")
                    sent_taolink = True
                    last_turmass_at = now
                # Keep only a tail so repeated prompts are still detected.
                if len(buf) > 64:
                    del buf[:-64]

        raise TimeoutError(f"timeout waiting for BootROM ok; received {bytes(buf)!r}")

    def command(
        self,
        code: int,
        address: int,
        length: int,
        payload: bytes = b"",
        expected_code: int | None = None,
        expected_address: int | None = None,
        min_response_len: int = 7,
        timeout_s: float = 3.0,
    ) -> tuple[int, int, int]:
        self.write(pack_header(code, address, length) + payload)
        if expected_code is None:
            return (0, 0, 0)

        response = self.read_packet(timeout_s=timeout_s, min_len=min_response_len)
        if len(response) >= 7:
            rx_code, rx_address, rx_length = unpack_header(response)
        else:
            rx_code, rx_address, rx_length = response[0], 0, 0
        if rx_code != expected_code:
            raise RuntimeError(
                f"unexpected ack 0x{rx_code:02X}; expected 0x{expected_code:02X}; "
                f"raw={response.hex(' ')}"
            )
        if expected_address is not None and rx_address != expected_address:
            raise RuntimeError(
                f"unexpected ack address 0x{rx_address:08X}; expected 0x{expected_address:08X}"
            )
        return rx_code, rx_address, rx_length

    def handshake(
        self,
        reset: bool,
        module_label: str = "module",
        serial_reset: bool = False,
    ) -> None:
        debug("Handshake at 115200...")
        self.ser.reset_input_buffer()

        if serial_reset:
            self.pulse_serial_reset()
        elif reset:
            self.write(b"AT+RST\r\n")

        last_error: Exception | None = None

        if serial_reset or reset:
            try:
                data = self.bootrom_handshake_once(AUTO_RESET_HANDSHAKE_TIMEOUT_S)
                if b"ok" in data:
                    return
            except TimeoutError as exc:
                last_error = exc
                self.ser.reset_input_buffer()

        log(f"Press the {module_label} module reset key.")
        debug("Waiting for TurMass. Press Ctrl+C to abort.")
        try:
            data = self.bootrom_handshake_once(None)
            if b"ok" in data:
                return
        except TimeoutError as exc:
            last_error = exc

        raise TimeoutError(f"BootROM handshake failed after manual reset: {last_error}")

    def get_version(self) -> None:
        debug("Get BootROM version...")
        self.command(
            OPCODE_VERSION_GET,
            0,
            0,
            expected_code=OPCODE_VERSION_GET_ACK,
            min_response_len=1,
        )

    def download_patch(self, patch: bytes) -> None:
        debug("Download RAM patch...")
        offsets = list(range(0, len(patch), BOOTPATCH_CHUNK))
        for index, offset in enumerate(offsets, 1):
            address = BOOTPATCH_BASE + offset
            chunk = patch[offset : offset + BOOTPATCH_CHUNK]
            debug(f"  patch block 0x{address:08X}")
            self.command(
                OPCODE_WRITE_RAM,
                address,
                len(chunk),
                chunk,
                expected_code=OPCODE_WRITE_RAM_ACK,
                expected_address=address,
            )
            progress("Prepare", index, len(offsets))

    def execute_patch(self, label: str) -> None:
        debug(label)
        self.command(
            OPCODE_EXECUTE_CODE,
            BOOTPATCH_BASE,
            0,
            expected_code=OPCODE_EXECUTE_CODE_END,
            min_response_len=1,
            timeout_s=5.0,
        )

    def change_baudrate(self) -> None:
        if self.target_baud == 115200:
            return
        debug(f"Switch baudrate to {self.target_baud}...")
        self.command(
            OPCODE_CHANGE_BAUDRATE,
            self.target_baud,
            0,
            expected_code=OPCODE_CHANGE_BAUDRATE_ACK,
            min_response_len=1,
        )
        self.ser.baudrate = self.target_baud
        time.sleep(0.05)

    def erase_for_local_burn(self, erase_user_params: bool) -> None:
        debug("Erase flash blocks 0x00000..0x3FFFF...")
        erase_blocks = list(range(0, FLASH_ERASE_END, 0x10000))
        for index, address in enumerate(erase_blocks, 1):
            debug(f"  erase block 0x{address:05X}")
            self.command(
                OPCODE_BLOCK64K_ERASE,
                address,
                0,
                expected_code=OPCODE_BLOCK64K_ERASE_ACK,
                expected_address=address,
                timeout_s=8.0,
            )
            progress("Erase", index, len(erase_blocks))

        if not erase_user_params:
            debug("Keep ELRS user parameters 0x69000..0x7EFFF.")
            return

        debug("Erase ELRS user parameters 0x69000..0x7EFFF, keep Rx OTA metadata 0x7D000...")
        erase_addresses = [
            address
            for address in range(ELRS_USER_PARAM_START, ELRS_USER_PARAM_END, FLASH_SECTOR_SIZE)
            if address != RX_OTA_META_OFFSET
        ]
        for index, address in enumerate(erase_addresses, 1):
            debug(f"  erase sector 0x{address:05X}")
            self.command(
                OPCODE_ERASE,
                address,
                0,
                expected_code=OPCODE_ERASE_ACK,
                expected_address=address,
                timeout_s=10.0,
            )
            progress("Erase parameters", index, len(erase_addresses))

    def erase_sectors(self, start: int, length: int, label: str) -> None:
        last = start + length
        erase_addresses = list(sector_range(start, length))
        debug(f"Erase {label} 0x{start:05X}..0x{last - 1:05X}...")
        for index, address in enumerate(erase_addresses, 1):
            debug(f"  erase sector 0x{address:05X}")
            self.command(
                OPCODE_ERASE,
                address,
                0,
                expected_code=OPCODE_ERASE_ACK,
                expected_address=address,
                timeout_s=10.0,
            )
            progress("Erase", index, len(erase_addresses))

    def write_local_image(self, image: bytes, label: str) -> None:
        debug(f"Write {label} image 0x00000..0x2FFFF...")
        for address in range(0, FLASH_IMAGE_SIZE, FLASH_WRITE_CHUNK):
            chunk = image[address : address + FLASH_WRITE_CHUNK]
            self.command(
                OPCODE_WRITE,
                address,
                len(chunk),
                chunk,
                expected_code=OPCODE_WRITE_ACK,
                expected_address=address,
                timeout_s=5.0,
            )
            written = address + len(chunk)
            progress("Write", written, FLASH_IMAGE_SIZE)
            if address and address % 0x4000 == 0:
                debug(f"  wrote 0x{address:05X}/0x{FLASH_IMAGE_SIZE:05X}")

    def write_flash(self, address: int, data: bytes, label: str) -> None:
        end = address + len(data)
        debug(f"Write {label} 0x{address:05X}..0x{end - 1:05X}...")
        offsets = list(range(0, len(data), FLASH_WRITE_CHUNK))
        for offset in offsets:
            chunk = data[offset : offset + FLASH_WRITE_CHUNK]
            chunk_address = address + offset
            self.command(
                OPCODE_WRITE,
                chunk_address,
                len(chunk),
                chunk,
                expected_code=OPCODE_WRITE_ACK,
                expected_address=chunk_address,
                timeout_s=5.0,
            )
            progress("Write", offset + len(chunk), len(data))
            if offset and offset % 0x4000 == 0:
                debug(f"  wrote 0x{offset:05X}/0x{len(data):05X}")

    def read_flash(self, address: int, length: int, label: str) -> bytes:
        end = address + length
        debug(f"Read back {label} 0x{address:05X}..0x{end - 1:05X}...")
        data = bytearray()
        offsets = list(range(0, length, FLASH_READ_CHUNK))
        for offset in offsets:
            chunk_len = min(FLASH_READ_CHUNK, length - offset)
            chunk_address = address + offset
            self.write(pack_header(OPCODE_READ, chunk_address, chunk_len))
            response = self.read_packet(timeout_s=5.0, min_len=7 + chunk_len)
            if len(response) < 7 + chunk_len:
                raise RuntimeError(
                    f"short read response at 0x{chunk_address:05X}: "
                    f"got 0x{len(response):X}, expected 0x{7 + chunk_len:X}"
                )
            rx_code, rx_address, rx_length = unpack_header(response)
            if rx_code != OPCODE_READ_ACK:
                raise RuntimeError(
                    f"unexpected read ack 0x{rx_code:02X}; expected 0x{OPCODE_READ_ACK:02X}; "
                    f"raw={response[:16].hex(' ')}"
                )
            if rx_address != chunk_address:
                raise RuntimeError(
                    f"unexpected read address 0x{rx_address:08X}; expected 0x{chunk_address:08X}"
                )
            if rx_length != chunk_len:
                raise RuntimeError(
                    f"unexpected read length 0x{rx_length:X}; expected 0x{chunk_len:X}"
                )
            data.extend(response[7 : 7 + chunk_len])
            progress("Read", offset + chunk_len, length)
            if offset and offset % 0x4000 == 0:
                debug(f"  read 0x{offset:05X}/0x{length:05X}")
        return bytes(data)

    def verify_crc32(self, image: bytes) -> None:
        self.verify_crc32_range(0, len(image), binascii.crc32(image) & 0xFFFFFFFF, "Tx image")

    def verify_crc32_range(self, flash_offset: int, length: int, expected: int, label: str) -> None:
        payload = struct.pack("<II", length, 0)
        debug(f"Verify {label} CRC32...")
        _, actual, _ = self.command(
            OPCODE_CALC_CRC32,
            XIP_FLASH_BASE + flash_offset,
            len(payload),
            payload,
            expected_code=OPCODE_CALC_CRC32_ACK,
            timeout_s=8.0,
        )
        debug(f"  target=0x{actual:08X} local=0x{expected:08X}")
        if actual != expected:
            raise RuntimeError("CRC32 verify failed")

    def disconnect(self) -> None:
        debug("Disconnect...")
        self.command(OPCODE_DISCONNECT, 0, 0)


def run_local(args: argparse.Namespace, kind: str) -> int:
    global VERBOSE
    VERBOSE = args.verbose

    bootloader_path = Path(args.bootloader)
    firmware_path = Path(args.firmware)
    patch_header = Path(args.patch_header)

    label = kind.upper()
    image = build_local_image(label, bootloader_path, firmware_path)
    patch = parse_patch_8620(patch_header)
    crc = binascii.crc32(image) & 0xFFFFFFFF

    debug(f"Bootloader size: 0x{len(parse_strhex(bootloader_path)):X}")
    debug(f"{label} firmware size: 0x{len(parse_strhex(firmware_path)):X}")
    debug(f"{label} image size: 0x{len(image):X}")
    debug(f"{label} image CRC32: 0x{crc:08X}")
    debug(f"Patch size: 0x{len(patch):X}")
    debug(f"ELRS user parameters: {'keep' if args.keep_user_params else 'erase'}")

    if args.dry_run:
        log("Dry run complete. No serial data was sent.")
        return 0

    try:
        dev = Tk8620Bootrom(args.port, args.baud)
    except serial.SerialException as exc:
        log(f"Failed to open {args.port}: {exc}")
        log("Close any serial terminal, BurnTool, IDE monitor, or test script that may be using this port,")
        log("then unplug/replug the CP2102N adapter and rerun burn.cmd.")
        return 2

    try:
        dev.handshake(
            reset=False,
            module_label=label,
            serial_reset=True,
        )
        dev.get_version()
        dev.download_patch(patch)
        dev.execute_patch("Execute RAM patch...")
        dev.change_baudrate()
        dev.erase_for_local_burn(erase_user_params=not args.keep_user_params)
        dev.write_local_image(image, label)
        dev.verify_crc32_range(0, len(image), crc, f"{label} image")
        dev.execute_patch("Execute RAM patch for flash lock...")
        dev.disconnect()
        log("Done.")
        return 0
    except (TimeoutError, OSError) as exc:
        log(f"Burn failed: {exc}")
        log(f"Rerun burn.cmd and press the {label} module reset key when prompted.")
        return 3
    finally:
        dev.close()


def run_rx_stash(args: argparse.Namespace) -> int:
    global VERBOSE
    VERBOSE = args.verbose

    root = Path(__file__).resolve().parents[2]
    firmware_path = Path(args.firmware)
    patch_header = Path(args.patch_header)
    firmware = parse_strhex(firmware_path)
    if len(firmware) > RX_STAGE_IMAGE_SIZE:
        raise ValueError(
            f"Rx firmware is too large: 0x{len(firmware):X}, limit 0x{RX_STAGE_IMAGE_SIZE:X}"
        )
    if sector_erase_end(args.image_offset, len(firmware)) > RX_STAGE_RESERVED_OFFSET:
        raise ValueError(
            "Rx firmware erase range would touch reserved flash at "
            f"0x{RX_STAGE_RESERVED_OFFSET:05X}; refusing to continue"
        )

    firmware_crc = binascii.crc32(firmware) & 0xFFFFFFFF
    rx_version, rx_version_source = resolve_rx_version(args.rx_version, root, firmware)
    target_id = parse_target_id(args.target_id)
    meta = build_rx_ota_meta(
        firmware_len=len(firmware),
        firmware_crc=firmware_crc,
        image_offset=args.image_offset,
        version=rx_version,
        freq=args.freq,
        target_id=target_id,
    )
    patch = parse_patch_8620(patch_header)

    debug(f"Rx firmware size: 0x{len(firmware):X}")
    debug(f"Rx firmware CRC32: 0x{firmware_crc:08X}")
    debug(f"Rx firmware version: {rx_version[0]}.{rx_version[1]}.{rx_version[2]} ({rx_version_source})")
    debug(f"Rx OTA frequency: {args.freq}")
    debug(f"Rx OTA target ID: {target_id.hex().upper()}")
    debug(f"Rx stage image: 0x{args.image_offset:05X}..0x{args.image_offset + len(firmware) - 1:05X}")
    debug(f"Rx OTA metadata: 0x{RX_OTA_META_OFFSET:05X}..0x{RX_OTA_META_OFFSET + len(meta) - 1:05X}")
    debug(f"Patch size: 0x{len(patch):X}")

    if args.dry_run:
        log("Dry run complete. No serial data was sent.")
        return 0

    try:
        dev = Tk8620Bootrom(args.port, args.baud)
    except serial.SerialException as exc:
        log(f"Failed to open {args.port}: {exc}")
        log("Close any serial terminal, BurnTool, IDE monitor, or test script that may be using this port,")
        log("then unplug/replug the CP2102N adapter and rerun burn.cmd.")
        return 2

    try:
        dev.handshake(reset=False, module_label="TX", serial_reset=True)
        dev.get_version()
        dev.download_patch(patch)
        dev.execute_patch("Execute RAM patch...")
        dev.change_baudrate()
        dev.erase_sectors(args.image_offset, len(firmware), "Rx firmware area")
        dev.write_flash(args.image_offset, firmware, "Rx firmware")
        readback = dev.read_flash(args.image_offset, len(firmware), "Rx firmware")
        if readback != firmware:
            for index, (expected, actual) in enumerate(zip(firmware, readback)):
                if expected != actual:
                    raise RuntimeError(
                        f"readback verify failed at flash 0x{args.image_offset + index:05X}: "
                        f"target=0x{actual:02X} local=0x{expected:02X}"
                    )
            raise RuntimeError("readback verify failed")
        debug("Readback verify OK.")
        dev.erase_sectors(RX_OTA_META_OFFSET, FLASH_SECTOR_SIZE, "Rx OTA metadata sector")
        dev.write_flash(RX_OTA_META_OFFSET, meta, "Rx OTA metadata")
        meta_readback = dev.read_flash(RX_OTA_META_OFFSET, len(meta), "Rx OTA metadata")
        if meta_readback != meta:
            raise RuntimeError("Rx OTA metadata readback verify failed")
        debug("Rx OTA metadata verify OK.")
        dev.execute_patch("Execute RAM patch for flash lock...")
        dev.disconnect()
        log("Done.")
        return 0
    except TimeoutError as exc:
        log(f"Burn failed: {exc}")
        log("Rerun burn.cmd and press the TX module reset key when prompted.")
        return 3
    finally:
        dev.close()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="TK8620 UART flasher")
    subparsers = parser.add_subparsers(dest="mode", required=True)

    def add_local_burn_parser(name: str, label: str) -> None:
        local = subparsers.add_parser(name, help=f"burn {label} bootloader + application image")
        local.add_argument("--port", required=True, help="serial port, e.g. COM10")
        local.add_argument("--baud", type=int, default=921600, help="post-patch baudrate")
        local.add_argument("--bootloader", required=True, help="bootloader strhex path")
        local.add_argument("--firmware", required=True, help=f"{label} application strhex path")
        local.add_argument(
            "--patch-header",
            default=str(Path(__file__).with_name("bootpatch.h")),
            help="BurnTool bootpatch.h containing patch_8620[]",
        )
        local.add_argument(
            "--keep-user-params",
            action="store_true",
            help="keep ELRS user parameters at 0x69000..0x7EFFF",
        )
        local.add_argument("--verbose", action="store_true", help="print detailed erase/write progress")
        local.add_argument("--dry-run", action="store_true", help="validate files and image layout only")

    add_local_burn_parser("tx", "Tx")
    add_local_burn_parser("rx", "Rx")

    def add_rx_stash_parser(name: str, help_text: str) -> None:
        rx_stash = subparsers.add_parser(name, help=help_text)
        rx_stash.add_argument("--port", required=True, help="serial port, e.g. COM10")
        rx_stash.add_argument("--baud", type=int, default=921600, help="post-patch baudrate")
        rx_stash.add_argument("--firmware", required=True, help="Rx application strhex path")
        rx_stash.add_argument(
            "--patch-header",
            default=str(Path(__file__).with_name("bootpatch.h")),
            help="BurnTool bootpatch.h containing patch_8620[]",
        )
        rx_stash.add_argument("--image-offset", type=lambda value: int(value, 0), default=RX_STAGE_IMAGE_OFFSET)
        rx_stash.add_argument("--rx-version", help="Rx OTA version MAJOR.MINOR.REVISION; defaults to the Rx firmware image version")
        rx_stash.add_argument("--freq", type=lambda value: int(value, 0), default=RX_OTA_DEFAULT_FREQ, help="Rx OTA frequency in Hz")
        rx_stash.add_argument("--target-id", help="8-byte Rx EFUSE ID as hex; default broadcasts to FFFFFFFFFFFFFFFF")
        rx_stash.add_argument("--verbose", action="store_true", help="print detailed erase/write progress")
        rx_stash.add_argument("--dry-run", action="store_true", help="validate files and stage layout only")

    add_rx_stash_parser("rx-stash", "stage Rx application image into Tx flash for wireless update")

    args = parser.parse_args(argv)
    if args.mode in ("tx", "rx"):
        return run_local(args, args.mode)
    if args.mode == "rx-stash":
        return run_rx_stash(args)
    raise SystemExit(f"unsupported mode: {args.mode}")


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
