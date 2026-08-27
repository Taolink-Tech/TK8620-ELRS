#!/usr/bin/env python3
"""Interactive application-level mode configurator for TK8620-ELRS unified firmware."""

from __future__ import annotations

import ctypes
import ctypes.wintypes
import secrets
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from typing import Iterable, Optional

try:
    import msvcrt
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - packaging/runtime diagnostic
    print("pyserial is missing. Run tools\\package-configurator.cmd to build the standalone tool.")
    raise SystemExit(2) from exc


MAGIC = b"TKELRSCF"
PROTOCOL_VERSION = 1
MAX_PAYLOAD = 20
CONFIG_BAUD = 115200
HELLO_INTERVAL_SECONDS = 0.010
PORT_RETRY_SECONDS = 0.100
AIRPORT_BAUD_RATES = (4800, 9600, 19200, 38400, 57600, 115200,
                      230400, 460800, 921600)
DEFAULT_AIRPORT_BAUD = 460800
SERIAL_RESET_LOW_SECONDS = 0.050
SERIAL_RESET_SETTLE_SECONDS = 0.020
AUTO_RESET_DISCOVERY_SECONDS = 1.000

CMD_HELLO = 1
CMD_HELLO_ACK = 2
CMD_GET_INFO = 3
CMD_GET_CONFIG = 4
CMD_SET_MODE = 5
CMD_REBOOT = 6
CMD_ERROR = 0x7F

ROLE_TX = 1
ROLE_RX = 2
MODE_RC = 0
MODE_AIRPORT = 1


class Cancelled(Exception):
    pass


class ProtocolError(RuntimeError):
    pass


@dataclass(frozen=True)
class PortIdentity:
    device: str
    vid: Optional[int]
    pid: Optional[int]
    serial_number: Optional[str]
    location: Optional[str]
    description: str

    @classmethod
    def from_port(cls, port) -> "PortIdentity":
        return cls(port.device, port.vid, port.pid, port.serial_number,
                   port.location, port.description or "")


@dataclass
class Frame:
    command: int
    sequence: int
    challenge: int
    payload: bytes


@dataclass
class DeviceInfo:
    role: int
    mode: int
    valid: bool
    protocol_version: int
    version: str
    build_id: str
    airport_baud: int = DEFAULT_AIRPORT_BAUD


@dataclass(frozen=True)
class DeviceConfig:
    mode: int
    airport_baud: int


def role_name(role: int) -> str:
    return "ELRS 900TX" if role == ROLE_TX else "ELRS 900RX" if role == ROLE_RX else f"Unknown({role})"


def mode_name(mode: int) -> str:
    return "AirPort" if mode == MODE_AIRPORT else "RC" if mode == MODE_RC else f"Unknown({mode})"


def choose_airport_baud() -> int:
    print("\nSelect the AirPort baud rate:")
    for index, baud in enumerate(AIRPORT_BAUD_RATES, 1):
        suffix = " (default)" if baud == DEFAULT_AIRPORT_BAUD else ""
        print(f"  [{index}] {baud}{suffix}")
    while True:
        value = input("Select a baud rate: ").strip()
        if value.isdigit() and 1 <= int(value) <= len(AIRPORT_BAUD_RATES):
            return AIRPORT_BAUD_RATES[int(value) - 1]
        print("Invalid selection.")


def build_frame(command: int, sequence: int, challenge: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    body = MAGIC + struct.pack("<BBBBI", PROTOCOL_VERSION, command, sequence,
                               len(payload), challenge) + payload
    return body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)


def parse_frames(buffer: bytearray) -> Iterable[Frame]:
    while True:
        start = buffer.find(MAGIC)
        if start < 0:
            if len(buffer) > len(MAGIC) - 1:
                del buffer[:-(len(MAGIC) - 1)]
            return
        if start:
            del buffer[:start]
        if len(buffer) < 12:
            return
        payload_length = buffer[11]
        if payload_length > MAX_PAYLOAD:
            del buffer[0]
            continue
        length = 20 + payload_length
        if len(buffer) < length:
            return
        raw = bytes(buffer[:length])
        del buffer[:length]
        expected = struct.unpack_from("<I", raw, length - 4)[0]
        if raw[8] != PROTOCOL_VERSION or (zlib.crc32(raw[:-4]) & 0xFFFFFFFF) != expected:
            continue
        _, command, sequence, _, challenge = struct.unpack_from("<BBBBI", raw, 8)
        yield Frame(command, sequence, challenge, raw[16:-4])


def q_pressed() -> bool:
    if not msvcrt.kbhit():
        return False
    key = msvcrt.getwch()
    return key.lower() == "q"


def available_ports():
    return list(list_ports.comports())


def choose_port_from_list(prompt: str, ports) -> PortIdentity:
    print(prompt)
    for index, port in enumerate(ports, 1):
        ids = ""
        if port.vid is not None and port.pid is not None:
            ids = f" VID:{port.vid:04X} PID:{port.pid:04X}"
        print(f"  [{index}] {port.device:<8} {port.description}{ids}")
    while True:
        value = input("Select a serial port: ").strip()
        if value.isdigit() and 1 <= int(value) <= len(ports):
            return PortIdentity.from_port(ports[int(value) - 1])
        print("Invalid selection.")


def choose_port(prompt: str) -> PortIdentity:
    while True:
        ports = available_ports()
        if not ports:
            print("No serial ports found. Connect a USB serial adapter and press Enter to retry.")
            input()
            continue
        return choose_port_from_list(prompt, ports)


def identity_score(identity: PortIdentity, port) -> int:
    score = 0
    if identity.serial_number and port.serial_number == identity.serial_number:
        score += 100
    if identity.location and port.location == identity.location:
        score += 40
    if identity.vid is not None and identity.pid is not None and (port.vid, port.pid) == (identity.vid, identity.pid):
        score += 20
    if identity.description and port.description == identity.description:
        score += 10
    if port.device == identity.device:
        score += 5
    return score


def find_matching_port(identity: PortIdentity) -> Optional[PortIdentity]:
    ports = available_ports()
    exact = [p for p in ports if p.device == identity.device]
    if exact:
        return PortIdentity.from_port(exact[0])
    scored = [(identity_score(identity, p), p) for p in ports]
    scored = [(score, port) for score, port in scored if score >= 20]
    if not scored:
        return None
    best = max(score for score, _ in scored)
    matches = [port for score, port in scored if score == best]
    if len(matches) == 1:
        return PortIdentity.from_port(matches[0])
    print("Multiple re-enumerated serial ports may match. Select the device again.")
    return choose_port("Available serial ports:")


def same_physical_port(identity: PortIdentity, port) -> bool:
    if port.device == identity.device:
        return True
    if identity.serial_number and port.serial_number == identity.serial_number:
        return True
    if identity.location and port.location == identity.location:
        return True
    return False


def serial_port_handle(port) -> ctypes.wintypes.HANDLE:
    handle = getattr(port, "_port_handle", None)
    if handle is None:
        raise OSError("Serial port handle is not available")
    value = handle.value if hasattr(handle, "value") else int(handle)
    if value is None or value == 0:
        raise OSError("Serial port handle is invalid")
    return ctypes.wintypes.HANDLE(value)


def set_rts_control_dcb(port, active: bool) -> None:
    import serial.win32 as win32

    dcb = win32.DCB()
    handle = serial_port_handle(port)
    if not win32.GetCommState(handle, ctypes.byref(dcb)):
        raise OSError(ctypes.get_last_error(), "GetCommState failed")
    dcb.fOutxCtsFlow = 0
    dcb.fRtsControl = win32.RTS_CONTROL_ENABLE if active else win32.RTS_CONTROL_DISABLE
    if not win32.SetCommState(handle, ctypes.byref(dcb)):
        raise OSError(ctypes.get_last_error(), "SetCommState failed")


def pulse_serial_reset(port) -> None:
    set_rts_control_dcb(port, True)
    time.sleep(SERIAL_RESET_LOW_SECONDS)
    set_rts_control_dcb(port, False)
    time.sleep(SERIAL_RESET_SETTLE_SECONDS)


def choose_remaining_port(excluded: PortIdentity) -> PortIdentity:
    """Select the other module without asking again when it is unambiguous."""
    print("\nAutomatically selecting the other device's serial port.")
    print("Press Q to cancel.")
    while True:
        if q_pressed():
            raise Cancelled()
        candidates = []
        for port in available_ports():
            if same_physical_port(excluded, port):
                continue
            candidates.append(port)
        if len(candidates) == 1:
            selected = PortIdentity.from_port(candidates[0])
            print(f"Automatically selected {selected.device}.")
            return selected
        if len(candidates) > 1:
            return choose_port_from_list("Multiple other serial ports found. Select the target device:", candidates)
        time.sleep(PORT_RETRY_SECONDS)


def read_matching_frame(port, buffer: bytearray, deadline: float, command: int,
                        sequence: int, challenge: int) -> Optional[Frame]:
    while time.monotonic() < deadline:
        if q_pressed():
            raise Cancelled()
        waiting = port.in_waiting
        data = port.read(waiting if waiting else 1)
        if data:
            buffer.extend(data)
            for frame in parse_frames(buffer):
                if (frame.command == command and frame.sequence == sequence and
                        frame.challenge == challenge):
                    return frame
        time.sleep(0.005)
    return None


def open_config_port(device: str):
    # The TX reset circuit AC-couples an RTS transition into a reset pulse.
    # pyserial defaults RTS/DTR to asserted when a port is opened directly, so
    # set the inactive states before assigning the port and opening it.
    port = serial.Serial(port=None, baudrate=CONFIG_BAUD,
                         bytesize=serial.EIGHTBITS,
                         parity=serial.PARITY_NONE,
                         stopbits=serial.STOPBITS_ONE, timeout=0,
                         write_timeout=0.2)
    port.rts = False
    port.dtr = False
    port.port = device
    try:
        port.open()
    except Exception:
        port.close()
        raise
    return port


def wait_for_device(identity: PortIdentity):
    manual_prompted = False
    auto_reset_deadline = None
    print("\nWaiting for the device.")
    print("The tool will first try to restart the selected device through RTS.")
    print("Press Q to cancel.")
    sequence = secrets.randbelow(255) + 1
    challenge = secrets.randbits(32)
    hello = build_frame(CMD_HELLO, sequence, challenge)
    current = identity
    port = None
    buffer = bytearray()
    next_hello = 0.0
    open_error_reported_for = None

    while True:
        if q_pressed():
            if port is not None:
                port.close()
            raise Cancelled()

        if port is None:
            matched = find_matching_port(current)
            if matched is None:
                time.sleep(PORT_RETRY_SECONDS)
                continue
            current = matched
            try:
                port = open_config_port(current.device)
                port.reset_input_buffer()
                buffer.clear()
                next_hello = 0.0
                open_error_reported_for = None
                try:
                    print(f"Attempting automatic restart on {current.device} through RTS.")
                    pulse_serial_reset(port)
                    auto_reset_deadline = time.monotonic() + AUTO_RESET_DISCOVERY_SECONDS
                    manual_prompted = False
                except (OSError, serial.SerialException) as exc:
                    auto_reset_deadline = None
                    print(f"Automatic reset failed: {exc}")
                    print("Restart the device manually now.")
                    manual_prompted = True
            except (OSError, serial.SerialException):
                if port is not None:
                    port.close()
                port = None
                if open_error_reported_for != current.device:
                    print(f"\nCannot open {current.device}. Another application may be using it.")
                    print("Close other configurators, flashers, or serial tools. This tool will keep retrying.")
                    open_error_reported_for = current.device
                time.sleep(PORT_RETRY_SECONDS)
                continue

        try:
            now = time.monotonic()
            if (auto_reset_deadline is not None and
                    now >= auto_reset_deadline and not manual_prompted):
                auto_reset_deadline = None
                print("No configuration handshake was detected after the automatic reset.")
                print("Restart the device manually now.")
                manual_prompted = True
            if now >= next_hello:
                port.write(hello)
                port.flush()
                next_hello = now + HELLO_INTERVAL_SECONDS

            waiting = port.in_waiting
            if waiting:
                buffer.extend(port.read(waiting))
                for frame in parse_frames(buffer):
                    if (frame.command != CMD_HELLO_ACK or
                            frame.sequence != sequence or
                            frame.challenge != challenge):
                        continue
                    if len(frame.payload) < 4 or frame.payload[0] not in (ROLE_TX, ROLE_RX):
                        raise ProtocolError("The device returned an invalid role")
                    print(f"\nConnected to {role_name(frame.payload[0])}. Reading device configuration.")
                    time.sleep(0.06)
                    port.reset_input_buffer()
                    return port, current, challenge, frame.payload[0]
            else:
                time.sleep(0.002)
        except Cancelled:
            port.close()
            raise
        except ProtocolError:
            port.close()
            raise
        except (OSError, serial.SerialException):
            port.close()
            port = None
            time.sleep(PORT_RETRY_SECONDS)


def transact(port, command: int, sequence: int, challenge: int,
             payload: bytes = b"", attempts: int = 3) -> Frame:
    raw = build_frame(command, sequence, challenge, payload)
    for _ in range(attempts):
        port.write(raw)
        port.flush()
        frame = read_matching_frame(port, bytearray(), time.monotonic() + 0.8,
                                    command, sequence, challenge)
        if frame is not None:
            return frame
    raise ProtocolError(f"No response to command 0x{command:02X}")


def decode_info(payload: bytes) -> DeviceInfo:
    if len(payload) < 6:
        raise ProtocolError("GET_INFO response is too short")
    role, mode, valid, protocol = payload[:4]
    pos = 4
    version_len = payload[pos]
    pos += 1
    if pos + version_len + 1 > len(payload):
        raise ProtocolError("GET_INFO version field is corrupt")
    version = payload[pos:pos + version_len].decode("ascii", "replace")
    pos += version_len
    build_len = payload[pos]
    pos += 1
    if pos + build_len > len(payload):
        raise ProtocolError("GET_INFO build field is corrupt")
    build_id = payload[pos:pos + build_len].decode("ascii", "replace")
    return DeviceInfo(role, mode, bool(valid), protocol, version, build_id)


def decode_config(payload: bytes) -> DeviceConfig:
    if len(payload) != 8:
        raise ProtocolError(
            "The firmware does not support configurable AirPort baud rates. Update the firmware and try again.")
    baud = struct.unpack_from("<I", payload, 4)[0]
    if payload[2] not in (MODE_RC, MODE_AIRPORT):
        raise ProtocolError(f"The device returned an invalid mode: {payload[2]}")
    if baud not in AIRPORT_BAUD_RATES:
        raise ProtocolError(f"The device returned an unsupported AirPort baud rate: {baud}")
    return DeviceConfig(payload[2], baud)


def open_session(identity: PortIdentity, expected_role: Optional[int] = None):
    port, current, challenge, hello_role = wait_for_device(identity)
    try:
        info = decode_info(transact(port, CMD_GET_INFO, 2, challenge).payload)
        config = transact(port, CMD_GET_CONFIG, 3, challenge).payload
        if len(config) < 4 or info.role != hello_role or config[1] != info.role:
            raise ProtocolError("Device role information is inconsistent")
        if expected_role is not None and info.role != expected_role:
            transact(port, CMD_REBOOT, 6, challenge)
            raise ProtocolError(
                f"Selected {role_name(info.role)}, but {role_name(expected_role)} is required")
        decoded = decode_config(config)
        info.valid = bool(config[0])
        info.mode = decoded.mode
        info.airport_baud = decoded.airport_baud
        return port, current, challenge, info
    except Exception:
        port.close()
        raise


def configure_one(expected_role: Optional[int], target_mode: Optional[int],
                  target_baud: Optional[int] = None,
                  identity: Optional[PortIdentity] = None, confirm: bool = True):
    if identity is None:
        prompt = (f"\nSelect the serial port for {role_name(expected_role)}:"
                  if expected_role is not None else "\nSelect a device serial port:")
        identity = choose_port(prompt)
    port, current, challenge, info = open_session(identity, expected_role)
    try:
        original = DeviceConfig(info.mode, info.airport_baud)
        print("\nDevice connected\n")
        print(f"Device: {role_name(info.role)}")
        print(f"Firmware: {info.version or info.build_id}")
        print(f"Configuration: {'Valid' if info.valid else 'Invalid; using safe defaults'}")
        print(f"Current mode: {mode_name(info.mode)}")
        print(f"AirPort baud: {info.airport_baud}")
        if target_mode is None:
            transact(port, CMD_REBOOT, 6, challenge)
            return info.role, current, original, original
        effective_baud = info.airport_baud if target_baud is None else target_baud
        target = DeviceConfig(target_mode, effective_baud)
        print(f"Target mode: {mode_name(target.mode)}")
        print(f"Target AirPort baud: {target.airport_baud}")
        if info.valid and original == target:
            print("The device already has the target configuration. Readback verification completed.")
        else:
            if confirm and input("\nApply this configuration? [Y/N] ").strip().lower() != "y":
                transact(port, CMD_REBOOT, 6, challenge)
                raise Cancelled()
            request = struct.pack("<BI", target.mode, target.airport_baud)
            response = transact(port, CMD_SET_MODE, 4, challenge, request).payload
            if (len(response) != 7 or response[0] != 0 or
                    response[1] != target.mode or not response[2] or
                    struct.unpack_from("<I", response, 3)[0] != target.airport_baud):
                raise ProtocolError("Configuration write or firmware readback verification failed")
            verify = transact(port, CMD_GET_CONFIG, 5, challenge).payload
            verified = decode_config(verify)
            if not verify[0] or verified != target:
                raise ProtocolError("Independent readback after the write failed")
            print("Write and independent readback verification succeeded.")
        transact(port, CMD_REBOOT, 6, challenge)
        print("The device is restarting.")
        return info.role, current, original, target
    finally:
        port.close()


def pair_flow(target_mode: int):
    completed = []
    roles = (ROLE_RX, ROLE_TX)
    target_baud = choose_airport_baud() if target_mode == MODE_AIRPORT else None
    print(f"\nConfigure a TX/RX pair for {mode_name(target_mode)}.")
    if target_baud is not None:
        print(f"AirPort baud: {target_baud}")
    if input("Continue? [Y/N] ").strip().lower() != "y":
        return
    try:
        first_identity = choose_port("\nSelect the serial port for either device:")
        role, identity, original, final = configure_one(None, target_mode, target_baud,
                                                         identity=first_identity,
                                                         confirm=False)
        completed.append((role, identity, original, final))
        remaining_role = ROLE_TX if role == ROLE_RX else ROLE_RX
        second_identity = choose_remaining_port(identity)
        role, identity, original, final = configure_one(remaining_role, target_mode, target_baud,
                                                         identity=second_identity,
                                                         confirm=False)
        completed.append((role, identity, original, final))
        print(f"\nConfiguration complete: RX and TX are both set to {mode_name(target_mode)}.")
        if target_mode == MODE_AIRPORT:
            print(f"Both devices use {target_baud} baud, 8N1.")
            print("Note: AirPort uses the existing binding. If user settings were erased during flashing, bind TX and RX again first.")
        return
    except (Cancelled, ProtocolError, OSError, serial.SerialException) as exc:
        if not isinstance(exc, Cancelled):
            print(f"\nConfiguration did not complete: {exc}")
    while completed:
        print("\nPair configuration is incomplete:")
        for role in roles:
            item = next((entry for entry in completed if entry[0] == role), None)
            status = (f"{mode_name(item[3].mode)}, {item[3].airport_baud} baud"
                      if item else "Not completed")
            print(f"{role_name(role)}: {status}")
        print("\n1. Wait and retry")
        print("2. Restore modified devices")
        print("0. Exit for now")
        choice = input("Select an option: ").strip()
        if choice == "1":
            done_roles = {entry[0] for entry in completed}
            try:
                for role in roles:
                    if role not in done_roles:
                        identity = choose_remaining_port(completed[0][1])
                        detected_role, identity, original, final = configure_one(role, target_mode, target_baud,
                                                                                  identity=identity,
                                                                                  confirm=False)
                        completed.append((detected_role, identity, original, final))
                print(f"\nConfiguration complete: RX and TX are both set to {mode_name(target_mode)}.")
                if target_mode == MODE_AIRPORT:
                    print(f"Both devices use {target_baud} baud, 8N1.")
                    print("Note: AirPort uses the existing binding. If user settings were erased during flashing, bind TX and RX again first.")
                return
            except (Cancelled, ProtocolError, OSError, serial.SerialException) as exc:
                if not isinstance(exc, Cancelled):
                    print(f"Retry failed: {exc}")
        elif choice == "2":
            for role, identity, original, _ in reversed(completed):
                try:
                    configure_one(role, original.mode, original.airport_baud,
                                  identity=identity, confirm=False)
                except Exception as exc:
                    print(f"Failed to restore {role_name(role)}: {exc}")
            print("Restore flow finished. Check each device result above.")
            return
        elif choice == "0":
            return


def query_pair_flow():
    print("\nRead the current configuration of a TX/RX pair.")
    try:
        first_identity = choose_port("\nSelect the serial port for either device:")
        first_role, identity, original, _ = configure_one(None, None,
                                                           identity=first_identity)
        second_identity = choose_remaining_port(identity)
        remaining_role = ROLE_TX if first_role == ROLE_RX else ROLE_RX
        second_role, _, second_mode, _ = configure_one(remaining_role, None,
                                                        identity=second_identity)
        print("\nDevice configuration readback complete:")
        print(f"{role_name(first_role)}: {mode_name(original.mode)}, {original.airport_baud} baud")
        print(f"{role_name(second_role)}: {mode_name(second_mode.mode)}, {second_mode.airport_baud} baud")
        if original != second_mode:
            print("WARNING: TX and RX configurations do not match.")
    except (Cancelled, ProtocolError, OSError, serial.SerialException) as exc:
        if not isinstance(exc, Cancelled):
            print(f"\nQuery did not complete: {exc}")


def main() -> int:
    if len(sys.argv) != 1:
        print("This tool does not accept command-line arguments. Run configure.cmd directly.")
        return 2
    try:
        while True:
            print("\nTK8620-ELRS Configurator\n")
            print("1. Configure a TX/RX pair for AirPort")
            print("2. Configure a TX/RX pair for RC")
            print("3. Read a TX/RX pair configuration")
            print("0. Exit")
            choice = input("Select an option: ").strip()
            if choice == "1":
                pair_flow(MODE_AIRPORT)
            elif choice == "2":
                pair_flow(MODE_RC)
            elif choice == "3":
                query_pair_flow()
            elif choice == "0":
                return 0
    except KeyboardInterrupt:
        print("\nCancelled.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
