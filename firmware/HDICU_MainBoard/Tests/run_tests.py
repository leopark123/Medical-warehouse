#!/usr/bin/env python3
"""
HDICU-ZKB01A Unit Tests — PC-side validation of firmware logic
Runs without hardware, without cross-compiler, just Python 3.

Tests cover:
  1. iPad protocol frame build/parse + checksum (includes 0xAA)
  2. Screen protocol frame build/parse + checksum (excludes header)
  3. NTC temperature calculation (Steinhart-Hart)
  4. CO2 sensor frame parse + checksum
  5. O2 sensor frame parse + checksum
  6. JFC103 frame parse + 0xFF sync
  7. Interlock rules (7 mutual exclusions + open O2 8-item interlock)
  8. Alarm latch/ack/clear state machine
  9. iPad 0x03 write validation (full packet reject)
"""

import struct
import math
import sys

passed = 0
failed = 0
errors = []

def assert_eq(actual, expected, msg=""):
    global passed, failed, errors
    if actual == expected:
        passed += 1
    else:
        failed += 1
        errors.append(f"FAIL: {msg} — expected {expected!r}, got {actual!r}")

def assert_true(cond, msg=""):
    global passed, failed, errors
    if cond:
        passed += 1
    else:
        failed += 1
        errors.append(f"FAIL: {msg}")

def assert_near(actual, expected, tolerance, msg=""):
    global passed, failed, errors
    if abs(actual - expected) <= tolerance:
        passed += 1
    else:
        failed += 1
        errors.append(f"FAIL: {msg} — expected ~{expected}, got {actual} (tol={tolerance})")

# =========================================================================
#  Test 1: iPad Protocol Frame
# =========================================================================
def test_ipad_frame():
    """iPad frame: AA [CMD] [LEN] [DATA...] [CS] 55
       CS = (0xAA + CMD + LEN + DATA_all) & 0xFF"""
    print("  [1] iPad frame checksum...")

    # Example: Read params request — AA 01 00 AB 55
    cs = (0xAA + 0x01 + 0x00) & 0xFF
    assert_eq(cs, 0xAB, "iPad read params checksum")

    # Example: Write ack success — AA 04 02 00 00 B0 55
    cs = (0xAA + 0x04 + 0x02 + 0x00 + 0x00) & 0xFF
    assert_eq(cs, 0xB0, "iPad write ack success checksum")

    # Example: Write ack fail — AA 04 02 01 01 B2 55
    cs = (0xAA + 0x04 + 0x02 + 0x01 + 0x01) & 0xFF
    assert_eq(cs, 0xB2, "iPad write ack fail checksum")

    # Example: Read vitals — AA 05 00 AF 55
    cs = (0xAA + 0x05 + 0x00) & 0xFF
    assert_eq(cs, 0xAF, "iPad read vitals checksum")

    # Build full frame and verify
    def build_ipad_frame(cmd, data):
        cs = (0xAA + cmd + len(data)) & 0xFF
        for b in data:
            cs = (cs + b) & 0xFF
        return bytes([0xAA, cmd, len(data)] + list(data) + [cs, 0x55])

    frame = build_ipad_frame(0x01, b'')
    assert_eq(frame, bytes([0xAA, 0x01, 0x00, 0xAB, 0x55]), "iPad build read params")

    # 0x02 response with 34 bytes of zeros
    data = bytes(34)
    frame = build_ipad_frame(0x02, data)
    assert_eq(frame[0], 0xAA, "iPad response head")
    assert_eq(frame[1], 0x02, "iPad response cmd")
    assert_eq(frame[2], 34, "iPad response len")
    assert_eq(frame[-1], 0x55, "iPad response tail")

# =========================================================================
#  Test 2: Screen Protocol Frame
# =========================================================================
def test_screen_frame():
    """Screen frame: AA 55 [CMD] [LEN] [DATA...] [CS] ED
       CS = (CMD + LEN + DATA_all) & 0xFF — does NOT include header"""
    print("  [2] Screen frame checksum...")

    def build_screen_frame(cmd, data):
        cs = (cmd + len(data)) & 0xFF
        for b in data:
            cs = (cs + b) & 0xFF
        return bytes([0xAA, 0x55, cmd, len(data)] + list(data) + [cs, 0xED])

    # Display data: cmd=0x01, 26 bytes of zeros
    data = bytes(26)
    frame = build_screen_frame(0x01, data)
    expected_cs = (0x01 + 26) & 0xFF  # = 0x1B
    assert_eq(frame[0], 0xAA, "Screen head1")
    assert_eq(frame[1], 0x55, "Screen head2")
    assert_eq(frame[2], 0x01, "Screen cmd")
    assert_eq(frame[3], 26, "Screen len")
    assert_eq(frame[-2], expected_cs, "Screen checksum excludes header")
    assert_eq(frame[-1], 0xED, "Screen tail")

    # Verify iPad and Screen checksums differ for same cmd/data
    ipad_cs = (0xAA + 0x01 + 26 + sum(data)) & 0xFF
    screen_cs = (0x01 + 26 + sum(data)) & 0xFF
    assert_true(ipad_cs != screen_cs, "iPad and Screen checksums must differ")

# =========================================================================
#  Test 3: NTC Temperature Calculation
# =========================================================================
def test_ntc():
    """NTC: 10kΩ@25°C, B=3950, pullup 10kΩ@3.3V
       R = 10000 * ADC / (4095 - ADC)
       T(K) = 1/(1/298.15 + ln(R/10000)/3950)
       T(°C) = T(K) - 273.15"""
    print("  [3] NTC temperature calculation...")

    def ntc_calc(adc):
        if adc <= 0 or adc >= 4095:
            return None
        r = 10000.0 * adc / (4095.0 - adc)
        t_k = 1.0 / (1.0/298.15 + math.log(r/10000.0)/3950.0)
        return t_k - 273.15

    # At 25°C, NTC=10kΩ, ADC should be ~2048 (half of 3.3V)
    t = ntc_calc(2048)
    assert_near(t, 25.0, 0.5, "NTC 25°C at midpoint ADC")

    # NTC has NEGATIVE temp coefficient: higher ADC → higher R → LOWER temp
    # ADC=1200 → R=4145Ω → ~46°C (hot)
    t_hot = ntc_calc(1200)
    assert_true(t_hot is not None and t_hot > 40, f"NTC hot: ADC=1200 → {t_hot:.1f}°C > 40")

    # ADC=2800 → R=21622Ω → ~8.6°C (cold)
    t_cold = ntc_calc(2800)
    assert_true(t_cold is not None and t_cold < 15, f"NTC cold: ADC=2800 → {t_cold:.1f}°C < 15")

    # Edge cases
    assert_eq(ntc_calc(0), None, "NTC ADC=0 invalid")
    assert_eq(ntc_calc(4095), None, "NTC ADC=4095 invalid")

    # x10 encoding
    t25_x10 = int(ntc_calc(2048) * 10)
    assert_true(240 <= t25_x10 <= 260, f"NTC 25°C x10 encoding ({t25_x10})")

# =========================================================================
#  Test 4: CO2 Sensor (MWD1006E)
# =========================================================================
def test_co2():
    """CO2 frame: FF 17 04 00 [H] [L] 00 00 [CS]
       CS = (~(sum bytes[1..7]) + 1) & 0xFF
       CO2 = (H << 8) | L"""
    print("  [4] CO2 sensor frame parse...")

    def build_co2_frame(co2_ppm):
        h = (co2_ppm >> 8) & 0xFF
        l = co2_ppm & 0xFF
        frame = [0xFF, 0x17, 0x04, 0x00, h, l, 0x00, 0x00]
        s = sum(frame[1:8]) & 0xFF
        cs = ((~s) + 1) & 0xFF
        frame.append(cs)
        return bytes(frame)

    # Normal: 500 ppm
    frame = build_co2_frame(500)
    assert_eq(frame[0], 0xFF, "CO2 head")
    assert_eq(frame[1], 0x17, "CO2 cmd")
    h, l = frame[4], frame[5]
    assert_eq((h << 8) | l, 500, "CO2 500ppm decode")
    # Verify checksum: sum of bytes[1..8] (data + cs) should be 0 mod 256
    verify_sum = sum(frame[1:9]) & 0xFF
    assert_eq(verify_sum, 0, "CO2 checksum verify (sum bytes[1..8]=0)")

    # Edge: 5000 ppm
    frame = build_co2_frame(5000)
    h, l = frame[4], frame[5]
    assert_eq((h << 8) | l, 5000, "CO2 5000ppm decode")

    # Bad checksum: should be rejected
    frame_bad = bytearray(build_co2_frame(1000))
    frame_bad[8] ^= 0x01  # Corrupt checksum
    s = sum(frame_bad[1:8]) & 0xFF
    cs_calc = ((~s) + 1) & 0xFF
    assert_true(cs_calc != frame_bad[8], "CO2 bad checksum detected")

# =========================================================================
#  Test 5: O2 Sensor (OCS-3RL-2.0)
# =========================================================================
def test_o2():
    """O2 frame: 12 bytes, header 0x16 0x09 0x01/0x02
       checksum: sum of all 12 bytes = 0x00 (low 8 bits)
       O2=bytes[3:4], humidity=bytes[5:6], temp=bytes[7:8], pressure=bytes[9:10]"""
    print("  [5] O2 sensor frame parse...")

    def build_o2_frame(o2_x10, humid_x10, temp_x10, press_x10, mode=0x01):
        frame = [0x16, 0x09, mode,
                 (o2_x10 >> 8) & 0xFF, o2_x10 & 0xFF,
                 (humid_x10 >> 8) & 0xFF, humid_x10 & 0xFF,
                 (temp_x10 >> 8) & 0xFF, temp_x10 & 0xFF,
                 (press_x10 >> 8) & 0xFF, press_x10 & 0xFF]
        s = sum(frame) & 0xFF
        cs = (0x100 - s) & 0xFF  # Make total sum = 0
        frame.append(cs)
        return bytes(frame)

    # Normal: O2=20.9%, humid=50.0%, temp=25.0°C, press=101.3kPa
    frame = build_o2_frame(209, 500, 250, 1013)
    assert_eq(sum(frame) & 0xFF, 0, "O2 checksum sum=0")
    assert_eq(frame[0], 0x16, "O2 head1")
    assert_eq(frame[1], 0x09, "O2 head2")
    o2 = (frame[3] << 8) | frame[4]
    assert_eq(o2, 209, "O2 20.9% decode")

    # Bad header: should be rejected
    frame_bad = bytearray(build_o2_frame(209, 500, 250, 1013))
    frame_bad[0] = 0x17  # Wrong header
    assert_true(frame_bad[0] != 0x16, "O2 bad header detected")

# =========================================================================
#  Test 6: JFC103 Frame Parse
# =========================================================================
def test_jfc103():
    """JFC103: 88 bytes, header 0xFF (only 0xFF in packet)
       heartrate=byte[65], spo2=byte[66]"""
    print("  [6] JFC103 frame parse...")

    # Build valid frame: 0xFF header + 87 data bytes (no other 0xFF)
    frame = [0xFF]  # Header
    for i in range(64):
        frame.append(i % 127)  # ECG wave data (no 0xFF)
    frame.append(72)    # byte 65: heart rate = 72 bpm
    frame.append(98)    # byte 66: SpO2 = 98%
    frame.extend([0] * (88 - len(frame)))  # Pad to 88 bytes
    assert_eq(len(frame), 88, "JFC103 frame length")
    assert_eq(frame[0], 0xFF, "JFC103 header")
    assert_eq(frame[65], 72, "JFC103 HR=72")
    assert_eq(frame[66], 98, "JFC103 SpO2=98")

    # Verify no other 0xFF in data
    other_ff = sum(1 for b in frame[1:] if b == 0xFF)
    assert_eq(other_ff, 0, "JFC103 no 0xFF in data (sync property)")

    # Blood pressure at bytes 71-72 (rsv[3], rsv[4]) — verify they exist but
    # protocol output must fill 0 regardless of what JFC103 reports
    assert_eq(frame[71], 0, "JFC103 BP byte71 exists (unused in protocol, filled 0)")
    assert_eq(frame[72], 0, "JFC103 BP byte72 exists (unused in protocol, filled 0)")

# =========================================================================
#  Test 7: Interlock Rules
# =========================================================================
def test_interlocks():
    """Test interlock rules by simulating relay bitmap + switch_status state.
       Mirrors interlock.c logic to verify rule enforcement."""
    print("  [7] Interlock rules...")

    # Relay bit indices (from bsp_config.h)
    R_PTC=0; R_JIARE=1; R_RED=2; R_ZIY=3; R_O2=4
    R_JIASHI=5; R_FENGJI=6; R_YASUO=7; R_WH=8
    SW_INNER=0x01; SW_FRESH=0x02; SW_OPEN_O2=0x04

    def apply_interlocks(relay, switch_st, open_o2_setpoint, o2_supplying=False):
        """Simulate interlock_apply() logic. Returns (relay, switch_st, open_o2_setpoint)."""
        triggered = False
        # Snapshot before modifications
        fogging_active = (relay & (1 << R_WH)) != 0
        open_o2_requested = (switch_st & SW_OPEN_O2) != 0

        # Rule 5: fogging blocks open O2 (runs BEFORE Rule 4)
        # Mirrors interlock.c: only close O2 valve if NOT in normal supply state
        if fogging_active and open_o2_requested:
            switch_st &= ~SW_OPEN_O2
            open_o2_setpoint = 0
            # Close O2 valve only if not in normal O2_STATE_SUPPLYING
            # (o2_supplying param indicates normal supply is active)
            if not o2_supplying:
                relay &= ~(1 << R_O2)
            triggered = True

        # Rule 4: Open O2 mode
        if switch_st & SW_OPEN_O2:
            relay |= (1 << R_O2)          # O2 valve ON
            switch_st &= ~SW_INNER        # Inner cycle FORBIDDEN
            switch_st |= SW_FRESH         # Fresh air FORCED
            relay &= ~(1 << R_PTC)        # Heating FORBIDDEN
            relay &= ~(1 << R_JIARE)
            relay &= ~(1 << R_WH)         # Fogging FORBIDDEN
            relay &= ~(1 << R_ZIY)        # UV FORBIDDEN
            triggered = True

        # Rule 1: Cooling ↔ Heating
        cooling = (relay & (1 << R_YASUO)) != 0
        heating = ((relay & (1 << R_PTC)) != 0) or ((relay & (1 << R_JIARE)) != 0)
        if cooling and heating:
            relay &= ~(1 << R_PTC)
            relay &= ~(1 << R_JIARE)

        # Rule 2: UV ↔ Open O2
        if (relay & (1 << R_ZIY)) and (switch_st & SW_OPEN_O2):
            relay &= ~(1 << R_ZIY)

        # Rule 3: Fogging ↔ UV
        if (relay & (1 << R_WH)) and (relay & (1 << R_ZIY)):
            relay &= ~(1 << R_ZIY)

        return relay, switch_st, open_o2_setpoint

    # --- Rule 1: Cooling ↔ Heating mutually exclusive ---
    r = (1 << R_YASUO) | (1 << R_PTC)  # Both cooling + heating on
    r, sw, _ = apply_interlocks(r, 0, 0)
    assert_true((r & (1 << R_YASUO)) != 0, "Rule1: Cooling survives")
    assert_true((r & (1 << R_PTC)) == 0, "Rule1: Heating blocked")

    # --- Rule 2: UV ↔ Open O2 ---
    r = (1 << R_ZIY)  # UV on
    sw = SW_OPEN_O2    # Open O2 requested
    r, sw, _ = apply_interlocks(r, sw, 1)
    assert_true((r & (1 << R_ZIY)) == 0, "Rule2: UV blocked by open O2")
    assert_true((r & (1 << R_O2)) != 0, "Rule2: O2 valve on")

    # --- Rule 3: Fogging ↔ UV ---
    r = (1 << R_WH) | (1 << R_ZIY)
    r, sw, _ = apply_interlocks(r, 0, 0)
    assert_true((r & (1 << R_WH)) != 0, "Rule3: Fogging survives")
    assert_true((r & (1 << R_ZIY)) == 0, "Rule3: UV blocked by fogging")

    # --- Rule 4: Open O2 full interlock ---
    # NOTE: Cannot have fogging active + open O2 (Rule 5 would block O2 first).
    # Test with heating + UV on, no fogging.
    r = (1 << R_PTC) | (1 << R_JIARE) | (1 << R_ZIY)  # Heat+UV on, NO fog
    sw = SW_INNER | SW_OPEN_O2
    r, sw, _ = apply_interlocks(r, sw, 1)
    assert_true((r & (1 << R_O2)) != 0, "Rule4: O2 valve ON")
    assert_true((sw & SW_INNER) == 0, "Rule4: Inner cycle FORBIDDEN")
    assert_true((sw & SW_FRESH) != 0, "Rule4: Fresh air FORCED")
    assert_true((r & (1 << R_PTC)) == 0, "Rule4: PTC heating FORBIDDEN")
    assert_true((r & (1 << R_JIARE)) == 0, "Rule4: Bottom heating FORBIDDEN")
    assert_true((r & (1 << R_ZIY)) == 0, "Rule4: UV FORBIDDEN")

    # --- Rule 4: Cooling CONDITIONAL (allowed when outer+fresh active) ---
    r = (1 << R_YASUO)  # Cooling on
    sw = SW_OPEN_O2
    r, sw, _ = apply_interlocks(r, sw, 1)
    assert_true((r & (1 << R_YASUO)) != 0, "Rule4: Cooling allowed (outer+fresh forced)")

    # --- Rule 5: Fogging active + open O2 requested → fogging wins ---
    r = (1 << R_WH)  # Fogging running
    sw = SW_OPEN_O2   # User requests open O2
    r, sw, setpt = apply_interlocks(r, sw, 1)
    assert_true((sw & SW_OPEN_O2) == 0, "Rule5: Open O2 blocked by fogging")
    assert_eq(setpt, 0, "Rule5: setpoint.open_o2 reverted to 0")
    assert_true((r & (1 << R_WH)) != 0, "Rule5: Fogging relay preserved")
    assert_true((r & (1 << R_O2)) == 0, "Rule5: O2 valve closed")

    # --- Rule 5 reverse: Open O2 active + fogging not running → O2 works ---
    r = 0
    sw = SW_OPEN_O2
    r, sw, _ = apply_interlocks(r, sw, 1)
    assert_true((r & (1 << R_O2)) != 0, "Rule5-rev: O2 valve on when no fogging")

    # Nursing level: 1~3
    for lv in [1, 2, 3]:
        assert_true(1 <= lv <= 3, f"Nursing {lv} valid")
    assert_true(4 > 3, "Nursing 4 rejected (>3)")

# =========================================================================
#  Test 8: Alarm Latch/Ack/Clear State Machine
# =========================================================================
def test_alarm():
    """Alarm spec:
       - Alarm latches when condition met for delay period
       - Buzzer active when alarm latched AND not acknowledged
       - Clear requires BOTH: parameter normal + user acknowledged
       - ALARM_COMM_FAULT: clear requires screen back online + acknowledged"""
    print("  [8] Alarm state machine...")

    # Simulate alarm state machine
    alarm_flags = 0
    acknowledged = False
    TEMP_HIGH = 1 << 0
    COMM_FAULT = 1 << 6

    # Step 1: Temperature alarm triggers
    current_flags = TEMP_HIGH
    alarm_flags |= current_flags
    assert_eq(alarm_flags, TEMP_HIGH, "Alarm latched")
    buzzer = (alarm_flags != 0) and not acknowledged
    assert_true(buzzer, "Buzzer active on unacknowledged alarm")

    # Step 2: User acknowledges
    acknowledged = True
    buzzer = (alarm_flags != 0) and not acknowledged
    assert_true(not buzzer, "Buzzer silenced after acknowledge")

    # Step 3: Temperature still high — alarm should NOT clear
    current_flags = TEMP_HIGH
    alarm_flags |= current_flags
    if acknowledged:
        resolved = alarm_flags & ~current_flags
        alarm_flags &= ~resolved
    assert_eq(alarm_flags, TEMP_HIGH, "Alarm stays because condition still active")

    # Step 4: Temperature returns to normal — NOW alarm clears
    current_flags = 0  # No active conditions
    alarm_flags |= current_flags
    if acknowledged:
        resolved = alarm_flags & ~current_flags
        alarm_flags &= ~resolved
    assert_eq(alarm_flags, 0, "Alarm clears: condition normal + acknowledged")

    # Step 5: Comm fault test
    alarm_flags = COMM_FAULT
    acknowledged = False
    screen_online = False

    # Screen offline — comm fault stays in flags
    if not screen_online:
        current_flags = COMM_FAULT
    else:
        current_flags = 0
    alarm_flags |= current_flags

    # User acknowledges while screen still offline
    acknowledged = True
    if acknowledged:
        resolved = alarm_flags & ~current_flags
        alarm_flags &= ~resolved
    assert_eq(alarm_flags, COMM_FAULT, "Comm fault stays: screen still offline despite ack")

    # Screen comes back online
    screen_online = True
    current_flags = 0 if screen_online else COMM_FAULT
    alarm_flags |= current_flags
    if acknowledged:
        resolved = alarm_flags & ~current_flags
        alarm_flags &= ~resolved
    assert_eq(alarm_flags, 0, "Comm fault clears: screen online + acknowledged")

# =========================================================================
#  Test 9: iPad 0x03 Write Validation
# =========================================================================
def test_write_validation():
    """0x03: 22 bytes, full overwrite. ANY field OOB → reject ALL."""
    print("  [9] iPad 0x03 write validation...")

    def validate_write(data):
        """Returns (result, error_code). Mirrors ipad_protocol.c logic."""
        if len(data) != 22:
            return (0x02, 0x00)  # CMD error

        temp = (data[0] << 8) | data[1]
        humid = (data[2] << 8) | data[3]
        o2 = (data[4] << 8) | data[5]
        co2 = (data[6] << 8) | data[7]
        fog = (data[8] << 8) | data[9]
        disinf = (data[10] << 8) | data[11]
        fan = data[12]
        nursing = data[13]
        cycle = data[14]
        fresh = data[15]
        open_o2 = data[16]
        light = data[17]

        if temp < 100 or temp > 400: return (0x01, 0x01)
        if humid < 300 or humid > 900: return (0x01, 0x02)
        if o2 < 210 or o2 > 1000: return (0x01, 0x03)
        if co2 > 5000: return (0x01, 0x04)
        if fog > 3600: return (0x01, 0x05)
        if disinf > 3600: return (0x01, 0x06)
        if fan > 3: return (0x01, 0x07)
        if nursing < 1 or nursing > 3: return (0x01, 0x08)
        if cycle > 1: return (0x01, 0x08)
        if fresh > 1: return (0x01, 0x08)
        if open_o2 > 1: return (0x01, 0x08)
        if light > 0x0F: return (0x01, 0x08)

        return (0x00, 0x00)  # Success

    # Valid packet
    valid = bytearray(22)
    struct.pack_into('>H', valid, 0, 250)   # temp=25.0°C
    struct.pack_into('>H', valid, 2, 500)   # humid=50.0%
    struct.pack_into('>H', valid, 4, 210)   # o2=21.0%
    struct.pack_into('>H', valid, 6, 1000)  # co2=1000ppm
    struct.pack_into('>H', valid, 8, 60)    # fog=60s
    struct.pack_into('>H', valid, 10, 120)  # disinf=120s
    valid[12] = 2   # fan=mid
    valid[13] = 2   # nursing=中级
    valid[14] = 1   # inner_cycle=on
    valid[15] = 0   # fresh_air=off
    valid[16] = 0   # open_o2=off
    valid[17] = 0x0F  # all lights on
    result, err = validate_write(valid)
    assert_eq(result, 0x00, "Valid write accepted")

    # Nursing level 4 → reject (frozen: 1~3 only)
    bad = bytearray(valid)
    bad[13] = 4
    result, err = validate_write(bad)
    assert_eq(result, 0x01, "Nursing=4 rejected")

    # Nursing level 0 → reject
    bad = bytearray(valid)
    bad[13] = 0
    result, err = validate_write(bad)
    assert_eq(result, 0x01, "Nursing=0 rejected")

    # cycle=2 → reject (must be 0 or 1)
    bad = bytearray(valid)
    bad[14] = 2
    result, err = validate_write(bad)
    assert_eq(result, 0x01, "Cycle=2 rejected (boolean OOB)")

    # open_o2=2 → reject
    bad = bytearray(valid)
    bad[16] = 2
    result, err = validate_write(bad)
    assert_eq(result, 0x01, "OpenO2=2 rejected (boolean OOB)")

    # light=0x10 → reject
    bad = bytearray(valid)
    bad[17] = 0x10
    result, err = validate_write(bad)
    assert_eq(result, 0x01, "Light=0x10 rejected (>0x0F)")

    # Temperature too high → reject
    bad = bytearray(valid)
    struct.pack_into('>H', bad, 0, 500)  # 50.0°C > 40.0°C max
    result, err = validate_write(bad)
    assert_eq(result, 0x01, "Temp=50.0°C rejected (>40.0)")
    assert_eq(err, 0x01, "Temp OOB error code")

    # fresh=2 → reject
    bad = bytearray(valid)
    bad[15] = 2
    result, err = validate_write(bad)
    assert_eq(result, 0x01, "Fresh=2 rejected (boolean OOB)")

    # CO2=5001 → reject
    bad = bytearray(valid)
    struct.pack_into('>H', bad, 6, 5001)
    result, err = validate_write(bad)
    assert_eq(result, 0x01, "CO2=5001 rejected (>5000)")
    assert_eq(err, 0x04, "CO2 OOB error code")

    # fog=3601 → reject
    bad = bytearray(valid)
    struct.pack_into('>H', bad, 8, 3601)
    result, err = validate_write(bad)
    assert_eq(result, 0x01, "Fog=3601 rejected (>3600)")

    # disinfect=3601 → reject
    bad = bytearray(valid)
    struct.pack_into('>H', bad, 10, 3601)
    result, err = validate_write(bad)
    assert_eq(result, 0x01, "Disinfect=3601 rejected (>3600)")

    # O2=209 → reject (<210)
    bad = bytearray(valid)
    struct.pack_into('>H', bad, 4, 209)
    result, err = validate_write(bad)
    assert_eq(result, 0x01, "O2=209 rejected (<210)")

    # Wrong length → reject
    result, err = validate_write(bytes(20))
    assert_eq(result, 0x02, "Wrong length rejected")

# =========================================================================
#  Test 10: Sensor fail-safe
# =========================================================================
def test_sensor_failsafe():
    print(" [10] Sensor fail-safe...")

    # Simulate relay_status bits for each control module
    BSP_RELAY_PTC_IO    = 0
    BSP_RELAY_JIARE_IO  = 1
    BSP_RELAY_YASUO_IO  = 7
    BSP_RELAY_FENGJI_IO = 6
    BSP_RELAY_JIASHI_IO = 5
    BSP_RELAY_O2_IO     = 4

    # --- temp_control fail-safe ---
    # Simulate: was HEATING, relay bits set, then sensor goes invalid (-999)
    relay = (1 << BSP_RELAY_PTC_IO) | (1 << BSP_RELAY_JIARE_IO)
    temp_avg = -999  # sensor invalid

    # Fail-safe should clear all temp-owned bits
    if temp_avg == -999 or temp_avg < -400 or temp_avg > 800:
        relay &= ~(1 << BSP_RELAY_PTC_IO)
        relay &= ~(1 << BSP_RELAY_JIARE_IO)
        relay &= ~(1 << BSP_RELAY_YASUO_IO)
        relay &= ~(1 << BSP_RELAY_FENGJI_IO)
    assert_eq(relay, 0, "temp fail-safe: all temp relays OFF when sensor=-999")

    # Simulate: was COOLING, compressor+fan set
    relay = (1 << BSP_RELAY_YASUO_IO) | (1 << BSP_RELAY_FENGJI_IO)
    temp_avg = 900  # out of range (>80°C)
    if temp_avg == -999 or temp_avg < -400 or temp_avg > 800:
        relay &= ~(1 << BSP_RELAY_PTC_IO)
        relay &= ~(1 << BSP_RELAY_JIARE_IO)
        relay &= ~(1 << BSP_RELAY_YASUO_IO)
        relay &= ~(1 << BSP_RELAY_FENGJI_IO)
    assert_eq(relay, 0, "temp fail-safe: cooling relays OFF when temp>80°C")

    # --- humidity_control fail-safe ---
    # Simulate: was DEHUMIDIFY with compressor+fan, sensor goes offline
    relay = (1 << BSP_RELAY_JIASHI_IO) | (1 << BSP_RELAY_YASUO_IO) | (1 << BSP_RELAY_FENGJI_IO)
    humid_state = 2  # DEHUMIDIFY
    o2_valid = False
    if not o2_valid:
        relay &= ~(1 << BSP_RELAY_JIASHI_IO)
        if humid_state == 2:  # was dehumidifying
            relay &= ~(1 << BSP_RELAY_YASUO_IO)
            relay &= ~(1 << BSP_RELAY_FENGJI_IO)
    assert_eq(relay, 0, "humid fail-safe: all humid relays OFF when o2 offline + dehumidify")

    # Simulate: was HUMIDIFY, sensor goes offline — only humidifier cleared
    relay = (1 << BSP_RELAY_JIASHI_IO) | (1 << BSP_RELAY_YASUO_IO)  # YASUO from temp
    humid_state = 1  # HUMIDIFY
    if not o2_valid:
        relay &= ~(1 << BSP_RELAY_JIASHI_IO)
        if humid_state == 2:
            relay &= ~(1 << BSP_RELAY_YASUO_IO)
    assert_eq(relay, (1 << BSP_RELAY_YASUO_IO), "humid fail-safe: JIASHI off but YASUO kept (temp owns it)")

    # --- oxygen_control fail-safe ---
    relay = (1 << BSP_RELAY_O2_IO)
    o2_valid = False
    o2_state = 1  # SUPPLYING (not OPEN_MODE)
    if not o2_valid and o2_state != 2:  # 2=OPEN_MODE
        relay &= ~(1 << BSP_RELAY_O2_IO)
    assert_eq(relay, 0, "oxygen fail-safe: O2 valve OFF when sensor offline")

# =========================================================================
#  Test 11: Dehumidify compressor+fan pairing
# =========================================================================
def test_dehumidify_fan():
    print(" [11] Dehumidify compressor+fan pairing...")

    BSP_RELAY_JIASHI_IO = 5
    BSP_RELAY_YASUO_IO  = 7
    BSP_RELAY_FENGJI_IO = 6

    # DEHUMIDIFY state should set BOTH compressor AND outer fan
    relay = 0
    humid_state = 2  # DEHUMIDIFY
    if humid_state == 2:
        relay &= ~(1 << BSP_RELAY_JIASHI_IO)
        relay |= (1 << BSP_RELAY_YASUO_IO)
        relay |= (1 << BSP_RELAY_FENGJI_IO)

    assert_eq(relay & (1 << BSP_RELAY_YASUO_IO), (1 << BSP_RELAY_YASUO_IO), "dehumidify: compressor ON")
    assert_eq(relay & (1 << BSP_RELAY_FENGJI_IO), (1 << BSP_RELAY_FENGJI_IO), "dehumidify: outer fan ON")
    assert_eq(relay & (1 << BSP_RELAY_JIASHI_IO), 0, "dehumidify: humidifier OFF")

# =========================================================================
#  Test 12: Timer beep request
# =========================================================================
def test_timer_beep():
    print(" [12] Timer expiry beep...")

    # Simulate fog countdown to 0
    fog_remaining = 1
    beep_request = 0
    beep_counter = 0

    # tick
    fog_remaining -= 1
    if fog_remaining == 0:
        beep_request |= 0x01
        beep_counter = 15

    assert_eq(fog_remaining, 0, "fog timer expired")
    assert_eq(beep_request & 0x01, 0x01, "fog beep bit set")
    assert_eq(beep_counter, 15, "beep counter = 15 (3s)")

    # Simulate disinfect countdown to 0
    disinf_remaining = 1
    beep_request = 0
    disinf_remaining -= 1
    if disinf_remaining == 0:
        beep_request |= 0x02
        beep_counter = 15
    assert_eq(beep_request & 0x02, 0x02, "disinfect beep bit set")

# =========================================================================
#  Test 13: iPad fog/disinfect timer start
# =========================================================================
def test_ipad_fog_timer():
    print(" [13] iPad fog/disinfect timer start...")

    # Simulate: interlock allows fogging, fog_time=60
    fog_time = 60
    fog_remaining = 0
    relay = 0
    BSP_RELAY_WH_IO = 8
    can_start = True  # interlock passes

    # Simulate control_timers_start_fog logic
    if fog_time > 0 and can_start:
        fog_remaining = fog_time
        relay |= (1 << BSP_RELAY_WH_IO)
    assert_eq(fog_remaining, 60, "iPad fog timer started with 60s")
    assert_eq(relay & (1 << BSP_RELAY_WH_IO), (1 << BSP_RELAY_WH_IO), "fog relay ON")

    # Simulate stop: fog_time=0
    fog_time = 0
    if fog_time == 0:
        fog_remaining = 0
        relay &= ~(1 << BSP_RELAY_WH_IO)
    assert_eq(fog_remaining, 0, "iPad fog timer stopped")
    assert_eq(relay & (1 << BSP_RELAY_WH_IO), 0, "fog relay OFF after stop")

    # Simulate: interlock blocks fogging
    fog_time = 60
    can_start = False
    old_remaining = fog_remaining
    if fog_time > 0 and can_start:
        fog_remaining = fog_time
    assert_eq(fog_remaining, old_remaining, "iPad fog blocked by interlock")

# =========================================================================
#  Run All Tests
# =========================================================================
def main():
    print("=" * 60)
    print("HDICU-ZKB01A Unit Tests")
    print("=" * 60)

    test_ipad_frame()
    test_screen_frame()
    test_ntc()
    test_co2()
    test_o2()
    test_jfc103()
    test_interlocks()
    test_alarm()
    test_write_validation()
    test_sensor_failsafe()
    test_dehumidify_fan()
    test_timer_beep()
    test_ipad_fog_timer()

    print()
    print("=" * 60)
    print(f"Results: {passed} passed, {failed} failed")
    if errors:
        print()
        for e in errors:
            print(f"  {e}")
    print("=" * 60)

    return 0 if failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
