#!/usr/bin/env python3
"""Interactive application-level mode configurator for TK8620-ELRS unified firmware."""

from __future__ import annotations

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
    print("缺少 pyserial。请运行 tools\\package-configurator.cmd 生成独立工具。")
    raise SystemExit(2) from exc


MAGIC = b"TKELRSCF"
PROTOCOL_VERSION = 1
MAX_PAYLOAD = 20
CONFIG_BAUD = 115200
HELLO_INTERVAL_SECONDS = 0.010
PORT_RETRY_SECONDS = 0.100

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


def role_name(role: int) -> str:
    return "ELRS 900TX" if role == ROLE_TX else "ELRS 900RX" if role == ROLE_RX else f"未知({role})"


def mode_name(mode: int) -> str:
    return "AirPort" if mode == MODE_AIRPORT else "RC" if mode == MODE_RC else f"未知({mode})"


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
        value = input("请选择串口：").strip()
        if value.isdigit() and 1 <= int(value) <= len(ports):
            return PortIdentity.from_port(ports[int(value) - 1])
        print("输入无效。")


def choose_port(prompt: str) -> PortIdentity:
    while True:
        ports = available_ports()
        if not ports:
            print("未发现串口，请连接 USB 串口后按回车重试。")
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
    print("检测到多个可能的重新枚举串口，请重新选择。")
    return choose_port("可用串口：")


def same_physical_port(identity: PortIdentity, port) -> bool:
    if port.device == identity.device:
        return True
    if identity.serial_number and port.serial_number == identity.serial_number:
        return True
    if identity.location and port.location == identity.location:
        return True
    return False


def choose_remaining_port(excluded: PortIdentity) -> PortIdentity:
    """Select the other module without asking again when it is unambiguous."""
    print("\n正在自动选择另一台设备的串口。")
    print("按 Q 取消。")
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
            print(f"已自动选择 {selected.device}。")
            return selected
        if len(candidates) > 1:
            return choose_port_from_list("检测到多个其他串口，请选择目标设备：", candidates)
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
    # On the onboard CP210x, asserted RTS holds the target in reset. pyserial
    # defaults RTS/DTR to asserted when a port is opened directly, so set the
    # inactive states before assigning the port and opening it.
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
    print("\n正在等待设备连接，请重启设备。")
    print("按 Q 取消。")
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
            except (OSError, serial.SerialException):
                if port is not None:
                    port.close()
                port = None
                if open_error_reported_for != current.device:
                    print(f"\n无法打开 {current.device}，串口可能被其他程序占用。")
                    print("请关闭其他配置、烧录或串口工具；本工具将继续重试。")
                    open_error_reported_for = current.device
                time.sleep(PORT_RETRY_SECONDS)
                continue

        try:
            now = time.monotonic()
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
                        raise ProtocolError("设备返回了非法类型")
                    print(f"\n已收到 {role_name(frame.payload[0])} 握手，正在读取设备配置。")
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
    raise ProtocolError(f"命令 0x{command:02X} 无响应")


def decode_info(payload: bytes) -> DeviceInfo:
    if len(payload) < 6:
        raise ProtocolError("GET_INFO 响应过短")
    role, mode, valid, protocol = payload[:4]
    pos = 4
    version_len = payload[pos]
    pos += 1
    if pos + version_len + 1 > len(payload):
        raise ProtocolError("GET_INFO 版本字段损坏")
    version = payload[pos:pos + version_len].decode("ascii", "replace")
    pos += version_len
    build_len = payload[pos]
    pos += 1
    if pos + build_len > len(payload):
        raise ProtocolError("GET_INFO 构建字段损坏")
    build_id = payload[pos:pos + build_len].decode("ascii", "replace")
    return DeviceInfo(role, mode, bool(valid), protocol, version, build_id)


def open_session(identity: PortIdentity, expected_role: Optional[int] = None):
    port, current, challenge, hello_role = wait_for_device(identity)
    try:
        info = decode_info(transact(port, CMD_GET_INFO, 2, challenge).payload)
        config = transact(port, CMD_GET_CONFIG, 3, challenge).payload
        if len(config) < 4 or info.role != hello_role or config[1] != info.role:
            raise ProtocolError("设备类型信息不一致")
        if expected_role is not None and info.role != expected_role:
            transact(port, CMD_REBOOT, 6, challenge)
            raise ProtocolError(
                f"选择的是 {role_name(info.role)}，当前需要 {role_name(expected_role)}")
        info.valid = bool(config[0])
        info.mode = config[2]
        return port, current, challenge, info
    except Exception:
        port.close()
        raise


def configure_one(expected_role: Optional[int], target_mode: Optional[int], identity: Optional[PortIdentity] = None,
                  confirm: bool = True):
    if identity is None:
        prompt = (f"\n请选择 {role_name(expected_role)} 使用的串口："
                  if expected_role is not None else "\n请选择设备串口：")
        identity = choose_port(prompt)
    port, current, challenge, info = open_session(identity, expected_role)
    try:
        print("\n设备连接成功\n")
        print(f"设备类型：{role_name(info.role)}")
        print(f"固件版本：{info.version or info.build_id}")
        print(f"配置状态：{'有效' if info.valid else '无效，当前安全回退 RC'}")
        print(f"当前模式：{mode_name(info.mode)}")
        if target_mode is None:
            transact(port, CMD_REBOOT, 6, challenge)
            return info.role, current, info.mode, info.mode
        print(f"目标模式：{mode_name(target_mode)}")
        if info.valid and info.mode == target_mode:
            print("设备已经处于目标模式，仅完成回读验证。")
        else:
            if confirm and input("\n是否修改？[Y/N] ").strip().lower() != "y":
                transact(port, CMD_REBOOT, 6, challenge)
                raise Cancelled()
            response = transact(port, CMD_SET_MODE, 4, challenge, bytes([target_mode])).payload
            if len(response) < 3 or response[0] != 0 or response[1] != target_mode or not response[2]:
                raise ProtocolError("模式写入或固件回读验证失败")
            verify = transact(port, CMD_GET_CONFIG, 5, challenge).payload
            if len(verify) < 4 or not verify[0] or verify[2] != target_mode:
                raise ProtocolError("写入后的独立回读验证失败")
            print("写入及回读验证成功。")
        transact(port, CMD_REBOOT, 6, challenge)
        print("设备正在重启。")
        return info.role, current, info.mode, target_mode
    finally:
        port.close()


def pair_flow(target_mode: int):
    completed = []
    roles = (ROLE_RX, ROLE_TX)
    print(f"\n将一对 TX/RX 切换到 {mode_name(target_mode)}。")
    if input("是否继续？[Y/N] ").strip().lower() != "y":
        return
    try:
        first_identity = choose_port("\n请选择任意一台设备的串口：")
        role, identity, original, final = configure_one(None, target_mode,
                                                         identity=first_identity,
                                                         confirm=False)
        completed.append((role, identity, original, final))
        remaining_role = ROLE_TX if role == ROLE_RX else ROLE_RX
        second_identity = choose_remaining_port(identity)
        role, identity, original, final = configure_one(remaining_role, target_mode,
                                                         identity=second_identity,
                                                         confirm=False)
        completed.append((role, identity, original, final))
        print(f"\n切换完成：RX 和 TX 均已设置为 {mode_name(target_mode)}。")
        if target_mode == MODE_AIRPORT:
            print("注意：AirPort 复用现有绑定。若烧录时曾清除用户参数，请先重新绑定 TX/RX。")
        return
    except (Cancelled, ProtocolError, OSError, serial.SerialException) as exc:
        if not isinstance(exc, Cancelled):
            print(f"\n配置未完成：{exc}")
    while completed:
        print("\n切换尚未完成：")
        for role in roles:
            item = next((entry for entry in completed if entry[0] == role), None)
            print(f"{role_name(role)}：{mode_name(item[3]) if item else '未完成'}")
        print("\n1. 继续等待或重试")
        print("2. 恢复已修改设备")
        print("0. 暂时退出")
        choice = input("请选择：").strip()
        if choice == "1":
            done_roles = {entry[0] for entry in completed}
            try:
                for role in roles:
                    if role not in done_roles:
                        identity = choose_remaining_port(completed[0][1])
                        detected_role, identity, original, final = configure_one(role, target_mode,
                                                                                  identity=identity,
                                                                                  confirm=False)
                        completed.append((detected_role, identity, original, final))
                print(f"\n切换完成：RX 和 TX 均已设置为 {mode_name(target_mode)}。")
                if target_mode == MODE_AIRPORT:
                    print("注意：AirPort 复用现有绑定。若烧录时曾清除用户参数，请先重新绑定 TX/RX。")
                return
            except (Cancelled, ProtocolError, OSError, serial.SerialException) as exc:
                if not isinstance(exc, Cancelled):
                    print(f"重试失败：{exc}")
        elif choice == "2":
            for role, identity, original, _ in reversed(completed):
                try:
                    configure_one(role, original, identity=identity, confirm=False)
                except Exception as exc:
                    print(f"{role_name(role)} 恢复失败：{exc}")
            print("恢复流程结束，请按上面的逐台结果确认。")
            return


def query_pair_flow():
    print("\n查看一对 TX/RX 当前配置。")
    try:
        first_identity = choose_port("\n请选择任意一台设备的串口：")
        first_role, identity, original, _ = configure_one(None, None,
                                                           identity=first_identity)
        second_identity = choose_remaining_port(identity)
        remaining_role = ROLE_TX if first_role == ROLE_RX else ROLE_RX
        second_role, _, second_mode, _ = configure_one(remaining_role, None,
                                                        identity=second_identity)
        print("\n设备状态读取完成：")
        print(f"{role_name(first_role)}：{mode_name(original)}")
        print(f"{role_name(second_role)}：{mode_name(second_mode)}")
    except (Cancelled, ProtocolError, OSError, serial.SerialException) as exc:
        if not isinstance(exc, Cancelled):
            print(f"\n查询未完成：{exc}")


def main() -> int:
    if len(sys.argv) != 1:
        print("本工具无需命令参数，请直接运行 configure.cmd。")
        return 2
    try:
        while True:
            print("\nTK8620-ELRS 模式配置工具\n")
            print("1. 将一对 TX/RX 切换到 AirPort")
            print("2. 将一对 TX/RX 切换到 RC")
            print("3. 查看一对设备当前配置")
            print("0. 退出")
            choice = input("请选择：").strip()
            if choice == "1":
                pair_flow(MODE_AIRPORT)
            elif choice == "2":
                pair_flow(MODE_RC)
            elif choice == "3":
                query_pair_flow()
            elif choice == "0":
                return 0
    except KeyboardInterrupt:
        print("\n已取消。")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
