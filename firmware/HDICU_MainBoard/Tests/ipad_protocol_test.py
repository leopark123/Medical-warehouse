#!/usr/bin/env python3
"""
iPad Protocol Test Script — HDICU-ZKB01A
Sends iPad protocol commands to UART2 and verifies responses.
Can replace iPad App for basic protocol verification.

Usage:
    python ipad_protocol_test.py COM5          # Use specific COM port
    python ipad_protocol_test.py COM5 --all    # Run all tests
    python ipad_protocol_test.py COM5 --read   # Only read params (0x01)
    python ipad_protocol_test.py COM5 --write  # Only write test (0x03)
    python ipad_protocol_test.py COM5 --vitals # Only read vitals (0x05)

Frame format: AA [CMD] [LEN] [DATA...] [CS] 55
Checksum: (0xAA + CMD + LEN + all DATA bytes) & 0xFF
"""

import serial
import struct
import sys
import time

# ========== Frame Builder ==========

def build_ipad_frame(cmd, data=b''):
    """Build an iPad protocol frame"""
    cs = (0xAA + cmd + len(data)) & 0xFF
    for b in data:
        cs = (cs + b) & 0xFF
    return bytes([0xAA, cmd, len(data)]) + data + bytes([cs, 0x55])

def parse_ipad_frame(raw):
    """Parse received iPad frame, returns (cmd, data) or None"""
    if len(raw) < 5:
        return None
    if raw[0] != 0xAA or raw[-1] != 0x55:
        return None
    cmd = raw[1]
    length = raw[2]
    if len(raw) != 3 + length + 2:
        return None
    data = raw[3:3+length]
    cs_rx = raw[3+length]
    cs_calc = (0xAA + cmd + length) & 0xFF
    for b in data:
        cs_calc = (cs_calc + b) & 0xFF
    if cs_rx != (cs_calc & 0xFF):
        return None
    return cmd, data

# ========== Test Functions ==========

def test_read_params(ser):
    """Send 0x01, expect 0x02 response (34 bytes)"""
    print("\n=== Test: Read Cabin Parameters (0x01 -> 0x02) ===")
    frame = build_ipad_frame(0x01)
    print(f"  TX: {frame.hex(' ')}")

    ser.reset_input_buffer()
    ser.write(frame)

    # Wait for response (max 500ms)
    time.sleep(0.2)
    rx = ser.read(100)

    if not rx:
        print("  RX: No response (timeout)")
        return False

    print(f"  RX: {rx.hex(' ')} ({len(rx)} bytes)")

    result = parse_ipad_frame(rx)
    if result is None:
        print("  FAIL: Invalid frame")
        return False

    cmd, data = result
    if cmd != 0x02:
        print(f"  FAIL: Expected CMD=0x02, got 0x{cmd:02X}")
        return False

    if len(data) != 34:
        print(f"  FAIL: Expected 34 bytes data, got {len(data)}")
        return False

    # Decode response
    temp = struct.unpack('>H', data[0:2])[0]
    humid = struct.unpack('>H', data[2:4])[0]
    o2 = struct.unpack('>H', data[4:6])[0]
    co2 = struct.unpack('>H', data[6:8])[0]
    fan = data[12]
    nursing = data[13]
    runtime = struct.unpack('>I', data[20:24])[0]

    print(f"  PASS: Decoded parameters:")
    print(f"    Temp={temp/10:.1f}°C  Humid={humid/10:.1f}%  O2={o2/10:.1f}%  CO2={co2}ppm")
    print(f"    Fan={fan}  Nursing={nursing}  Runtime={runtime}min")
    return True

def test_write_params(ser, valid=True):
    """Send 0x03 write, expect 0x04 response"""
    if valid:
        print("\n=== Test: Write Parameters - Valid (0x03 -> 0x04 OK) ===")
        data = bytearray(22)
        struct.pack_into('>H', data, 0, 250)   # temp=25.0°C
        struct.pack_into('>H', data, 2, 500)   # humid=50.0%
        struct.pack_into('>H', data, 4, 210)   # o2=21.0%
        struct.pack_into('>H', data, 6, 1000)  # co2=1000ppm
        struct.pack_into('>H', data, 8, 0)     # fog=0
        struct.pack_into('>H', data, 10, 0)    # disinfect=0
        data[12] = 1   # fan=low
        data[13] = 2   # nursing=中级
        data[14] = 0   # inner_cycle=off
        data[15] = 0   # fresh_air=off
        data[16] = 0   # open_o2=off
        data[17] = 0   # light=off
    else:
        print("\n=== Test: Write Parameters - Invalid nursing=4 (0x03 -> 0x04 FAIL) ===")
        data = bytearray(22)
        struct.pack_into('>H', data, 0, 250)
        struct.pack_into('>H', data, 2, 500)
        struct.pack_into('>H', data, 4, 210)
        struct.pack_into('>H', data, 6, 1000)
        data[12] = 1
        data[13] = 4   # INVALID: nursing=4 (max is 3)

    frame = build_ipad_frame(0x03, bytes(data))
    print(f"  TX: {frame.hex(' ')} ({len(frame)} bytes)")

    ser.reset_input_buffer()
    ser.write(frame)
    time.sleep(0.2)
    rx = ser.read(100)

    if not rx:
        print("  RX: No response")
        return False

    print(f"  RX: {rx.hex(' ')}")
    result = parse_ipad_frame(rx)
    if result is None:
        print("  FAIL: Invalid frame")
        return False

    cmd, rdata = result
    if cmd != 0x04 or len(rdata) != 2:
        print(f"  FAIL: Expected CMD=0x04 len=2")
        return False

    exec_result, err_code = rdata[0], rdata[1]

    if valid:
        if exec_result == 0x00:
            print(f"  PASS: Write accepted (result=0x00, err=0x00)")
            return True
        else:
            print(f"  FAIL: Write rejected (result=0x{exec_result:02X}, err=0x{err_code:02X})")
            return False
    else:
        if exec_result == 0x01:
            print(f"  PASS: Write correctly rejected (result=0x01, err=0x{err_code:02X})")
            return True
        else:
            print(f"  FAIL: Should have rejected but result=0x{exec_result:02X}")
            return False

def test_read_vitals(ser):
    """Send 0x05, expect 0x06 response (20 bytes)"""
    print("\n=== Test: Read Vitals (0x05 -> 0x06) ===")
    frame = build_ipad_frame(0x05)
    print(f"  TX: {frame.hex(' ')}")

    ser.reset_input_buffer()
    ser.write(frame)
    time.sleep(0.2)
    rx = ser.read(100)

    if not rx:
        print("  RX: No response")
        return False

    print(f"  RX: {rx.hex(' ')}")
    result = parse_ipad_frame(rx)
    if result is None:
        print("  FAIL: Invalid frame")
        return False

    cmd, data = result
    if cmd != 0x06 or len(data) != 20:
        print(f"  FAIL: Expected CMD=0x06 len=20")
        return False

    hr = struct.unpack('>H', data[0:2])[0]
    pulse = struct.unpack('>H', data[2:4])[0]
    spo2 = struct.unpack('>H', data[4:6])[0]
    # Bytes 6-13 should be 0 (BP/RR not supported)
    bp_zeros = all(b == 0 for b in data[6:14])

    print(f"  PASS: HR={hr} Pulse={pulse} SpO2={spo2} BP_zeros={bp_zeros}")
    return True

def test_bad_checksum(ser):
    """Send frame with wrong checksum, expect no response (silent discard)"""
    print("\n=== Test: Bad Checksum -> Silent Discard ===")
    frame = bytearray(build_ipad_frame(0x01))
    frame[-2] ^= 0xFF  # Corrupt checksum
    print(f"  TX: {frame.hex(' ')} (corrupted CS)")

    ser.reset_input_buffer()
    ser.write(frame)
    time.sleep(0.5)
    rx = ser.read(100)

    if not rx:
        print("  PASS: No response (correctly discarded)")
        return True
    else:
        print(f"  FAIL: Got response: {rx.hex(' ')}")
        return False

def test_unknown_cmd(ser):
    """Send unknown command, expect 0xFF error response"""
    print("\n=== Test: Unknown Command -> 0xFF Error ===")
    frame = build_ipad_frame(0xAB)  # Unknown command
    print(f"  TX: {frame.hex(' ')}")

    ser.reset_input_buffer()
    ser.write(frame)
    time.sleep(0.2)
    rx = ser.read(100)

    if not rx:
        print("  RX: No response")
        return False

    print(f"  RX: {rx.hex(' ')}")
    result = parse_ipad_frame(rx)
    if result is None:
        print("  FAIL: Invalid frame")
        return False

    cmd, data = result
    if cmd == 0xFF:
        print(f"  PASS: Error response (type=0x{data[0]:02X}, pos=0x{data[1]:02X})")
        return True
    else:
        print(f"  FAIL: Expected 0xFF, got 0x{cmd:02X}")
        return False

# ========== Main ==========

def main():
    if len(sys.argv) < 2:
        print("Usage: python ipad_protocol_test.py <COM_PORT> [--all|--read|--write|--vitals]")
        print("Example: python ipad_protocol_test.py COM5 --all")
        return

    port = sys.argv[1]
    mode = sys.argv[2] if len(sys.argv) > 2 else '--all'

    print(f"Opening {port} at 115200...")
    try:
        ser = serial.Serial(port, 115200, timeout=1)
    except Exception as e:
        print(f"Error: {e}")
        return

    print(f"Connected to {port}")
    passed = 0
    failed = 0

    tests = []
    if mode in ('--all', '--read'):
        tests.append(('Read Params', lambda: test_read_params(ser)))
    if mode in ('--all', '--write'):
        tests.append(('Write Valid', lambda: test_write_params(ser, True)))
        tests.append(('Write Invalid', lambda: test_write_params(ser, False)))
    if mode in ('--all', '--vitals'):
        tests.append(('Read Vitals', lambda: test_read_vitals(ser)))
    if mode == '--all':
        tests.append(('Bad Checksum', lambda: test_bad_checksum(ser)))
        tests.append(('Unknown CMD', lambda: test_unknown_cmd(ser)))

    for name, test_fn in tests:
        try:
            if test_fn():
                passed += 1
            else:
                failed += 1
        except Exception as e:
            print(f"  ERROR: {e}")
            failed += 1
        time.sleep(0.3)

    ser.close()
    print(f"\n{'='*50}")
    print(f"Results: {passed} passed, {failed} failed")
    print(f"{'='*50}")

if __name__ == '__main__':
    main()
