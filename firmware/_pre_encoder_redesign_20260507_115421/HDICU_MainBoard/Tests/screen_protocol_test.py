#!/usr/bin/env python3
"""
Screen Board Protocol Test Script — HDICU-ZKB01A
Simulates screen board to test dual-board communication.
Connects to UART1 and sends screen board commands.

Frame format: AA 55 [CMD] [LEN] [DATA...] [CS] ED
Checksum: (CMD + LEN + all DATA bytes) & 0xFF (does NOT include header AA 55)

Usage:
    python screen_protocol_test.py COM4              # Monitor received display data
    python screen_protocol_test.py COM4 --heartbeat  # Send heartbeat responses
    python screen_protocol_test.py COM4 --key 0x01   # Simulate key press (nursing level)
    python screen_protocol_test.py COM4 --alarm      # Send alarm acknowledge
"""

import serial
import struct
import sys
import time

# ========== Frame Builder ==========

def build_screen_frame(cmd, data=b''):
    """Build screen protocol frame: AA 55 CMD LEN DATA CS ED"""
    cs = (cmd + len(data)) & 0xFF
    for b in data:
        cs = (cs + b) & 0xFF
    return bytes([0xAA, 0x55, cmd, len(data)]) + data + bytes([cs, 0xED])

def parse_screen_frame(raw):
    """Parse screen frame from main controller"""
    if len(raw) < 6:
        return None
    # Find AA 55 header
    idx = 0
    while idx < len(raw) - 1:
        if raw[idx] == 0xAA and raw[idx+1] == 0x55:
            break
        idx += 1
    else:
        return None

    if idx + 4 > len(raw):
        return None

    cmd = raw[idx+2]
    length = raw[idx+3]

    if idx + 4 + length + 2 > len(raw):
        return None

    data = raw[idx+4 : idx+4+length]
    cs_rx = raw[idx+4+length]
    tail = raw[idx+4+length+1]

    if tail != 0xED:
        return None

    cs_calc = (cmd + length) & 0xFF
    for b in data:
        cs_calc = (cs_calc + b) & 0xFF

    if cs_rx != (cs_calc & 0xFF):
        return None

    return cmd, data

# ========== Monitor Mode ==========

def monitor_display_data(ser, duration=30):
    """Monitor 0x01 display data packets from main controller"""
    print(f"\n=== Monitoring Display Data for {duration}s ===")
    print("Expecting 0x01 packets every 100ms, 0x04 heartbeat every 1s\n")

    start = time.time()
    display_count = 0
    heartbeat_count = 0

    while time.time() - start < duration:
        rx = ser.read(100)
        if not rx:
            continue

        # Try to parse frames from received data
        result = parse_screen_frame(rx)
        if result is None:
            continue

        cmd, data = result

        if cmd == 0x01 and len(data) >= 26:
            display_count += 1
            temp = struct.unpack('>h', data[0:2])[0]
            humid = data[2]
            o2 = data[3]
            co2 = struct.unpack('>H', data[4:6])[0]
            hr = data[6]
            spo2 = data[7]
            fan = data[8]
            nursing = data[9]
            alarm = struct.unpack('>H', data[20:22])[0]

            if display_count % 10 == 1:  # Print every 1 second
                print(f"  [0x01] T={temp/10:.1f}°C H={humid}% O2={o2}% CO2={co2}ppm "
                      f"HR={hr} SpO2={spo2} Fan={fan} N={nursing} Alarm=0x{alarm:04X}")

        elif cmd == 0x04 and len(data) >= 8:
            heartbeat_count += 1
            runtime = struct.unpack('>I', data[0:4])[0]
            uptime = struct.unpack('>I', data[4:8])[0]
            print(f"  [0x04] Heartbeat: runtime={runtime}s uptime={uptime}s")

    print(f"\nReceived: {display_count} display packets, {heartbeat_count} heartbeats")
    return display_count > 0

# ========== Command Tests ==========

def send_heartbeat_ack(ser):
    """Send 0x84 heartbeat acknowledge"""
    print("\n=== Send Heartbeat ACK (0x84) ===")
    frame = build_screen_frame(0x84)
    print(f"  TX: {frame.hex(' ')}")
    ser.write(frame)
    print("  Sent. Should prevent COMM_FAULT alarm.")

def send_key_press(ser, key_id, action=0x01):
    """Send 0x82 key action"""
    key_names = {
        0x01: '护理等级灯', 0x02: '照明灯', 0x03: '检查灯',
        0x04: '红外灯', 0x05: '紫外灯', 0x06: '开放式供氧',
        0x07: '内/外循环', 0x08: '新风净化', 0x09: '报警确认',
        0x0A: '编码器按下'
    }
    action_names = {0x01: '单击', 0x02: '长按', 0x03: '按下', 0x04: '松开'}

    name = key_names.get(key_id, f'未知(0x{key_id:02X})')
    act = action_names.get(action, f'0x{action:02X}')
    print(f"\n=== Send Key: {name} {act} (0x82) ===")

    data = bytes([key_id, action])
    frame = build_screen_frame(0x82, data)
    print(f"  TX: {frame.hex(' ')}")
    ser.write(frame)
    print(f"  Sent key 0x{key_id:02X} action 0x{action:02X}")

def send_alarm_ack(ser, alarm_id=0xFF):
    """Send 0x85 alarm acknowledge"""
    print(f"\n=== Send Alarm ACK (0x85, ID=0x{alarm_id:02X}) ===")
    frame = build_screen_frame(0x85, bytes([alarm_id]))
    print(f"  TX: {frame.hex(' ')}")
    ser.write(frame)
    print("  Sent. Alarm should acknowledge (not clear until condition resolves).")

def send_timer_ctrl(ser, timer_type, cmd, duration=60):
    """Send 0x83 timer control"""
    type_names = {0x01: '雾化', 0x02: '消毒', 0x03: '供氧累计'}
    cmd_names = {0x01: '开始', 0x02: '停止', 0x03: '清零'}
    print(f"\n=== Timer Control: {type_names.get(timer_type,'')} {cmd_names.get(cmd,'')} ===")

    data = bytes([timer_type, cmd]) + struct.pack('>H', duration)
    frame = build_screen_frame(0x83, data)
    print(f"  TX: {frame.hex(' ')}")
    ser.write(frame)

# ========== Main ==========

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python screen_protocol_test.py <COM>              # Monitor display data")
        print("  python screen_protocol_test.py <COM> --heartbeat  # Send heartbeat")
        print("  python screen_protocol_test.py <COM> --key 0x01   # Key press (hex)")
        print("  python screen_protocol_test.py <COM> --alarm      # Alarm acknowledge")
        print("  python screen_protocol_test.py <COM> --fog 60     # Start fogging 60s")
        return

    port = sys.argv[1]
    mode = sys.argv[2] if len(sys.argv) > 2 else '--monitor'

    print(f"Opening {port} at 115200 (screen board protocol)...")
    ser = serial.Serial(port, 115200, timeout=0.5)

    if mode == '--monitor':
        monitor_display_data(ser, 30)
    elif mode == '--heartbeat':
        for i in range(10):
            send_heartbeat_ack(ser)
            time.sleep(1)
    elif mode == '--key':
        key_id = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x01
        send_key_press(ser, key_id)
    elif mode == '--alarm':
        send_alarm_ack(ser)
    elif mode == '--fog':
        duration = int(sys.argv[3]) if len(sys.argv) > 3 else 60
        send_timer_ctrl(ser, 0x01, 0x01, duration)
    else:
        print(f"Unknown mode: {mode}")

    ser.close()

if __name__ == '__main__':
    main()
