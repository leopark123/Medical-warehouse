# =====================================================================
#  HDICU 5-Issue Joint Diagnostic - Dump Parser
#  Reads dump_T0..T5.log, builds side-by-side summary.txt
# =====================================================================

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $here

# Field table: address (hex string, no 0x), size (bytes), signed flag, name
$fields = @(
    @{ Name = 'sensor.temperature[2]_x10';   Addr = '20000074'; Size = 2; Sign = $true  }
    @{ Name = 'sensor.temperature_avg_x10';  Addr = '20000078'; Size = 2; Sign = $true  }
    @{ Name = 'sensor.o2_percent';           Addr = '2000007E'; Size = 1; Sign = $false }
    @{ Name = 'sensor.o2_raw_x10';           Addr = '20000080'; Size = 2; Sign = $false }
    @{ Name = 'sensor.o2_valid';             Addr = '20000087'; Size = 1; Sign = $false }
    @{ Name = 'setpoint.target_temp_x10';    Addr = '2000008E'; Size = 2; Sign = $false }
    @{ Name = 'setpoint.fog_time_sec';       Addr = '20000096'; Size = 2; Sign = $false }
    @{ Name = 'setpoint.disinfect_time_sec'; Addr = '20000098'; Size = 2; Sign = $false }
    @{ Name = 'setpoint.fan_speed';          Addr = '2000009A'; Size = 1; Sign = $false }
    @{ Name = 'setpoint.nursing_level';      Addr = '2000009B'; Size = 1; Sign = $false }
    @{ Name = 'setpoint.inner_cycle';        Addr = '2000009C'; Size = 1; Sign = $false }
    @{ Name = 'setpoint.fresh_air';          Addr = '2000009D'; Size = 1; Sign = $false }
    @{ Name = 'setpoint.open_o2';            Addr = '2000009E'; Size = 1; Sign = $false }
    @{ Name = 'setpoint.light_ctrl';         Addr = '2000009F'; Size = 1; Sign = $false }
    @{ Name = 'control.temp_state';          Addr = '200000A4'; Size = 1; Sign = $false }
    @{ Name = 'control.fog_remaining_sec';   Addr = '200000A8'; Size = 2; Sign = $false }
    @{ Name = 'control.disinfect_remaining'; Addr = '200000AA'; Size = 2; Sign = $false }
    @{ Name = 'control.o2_accumulated_sec';  Addr = '200000AC'; Size = 2; Sign = $false }
    @{ Name = 'control.relay_status';        Addr = '200000AE'; Size = 2; Sign = $false }
    @{ Name = 'control.light_status';        Addr = '200000B0'; Size = 1; Sign = $false }
    @{ Name = 'control.switch_status';       Addr = '200000B1'; Size = 1; Sign = $false }
    @{ Name = 'control.fan_speed_actual';    Addr = '200000B4'; Size = 1; Sign = $false }
    @{ Name = 'control.nursing_lvl_actual';  Addr = '200000B5'; Size = 1; Sign = $false }
    @{ Name = 'alarm.alarm_flags';           Addr = '200000B6'; Size = 2; Sign = $false }
)

# Parse one JLink -log output. Two formats supported:
#   Format A (-log file, J-Link DLL trace, two lines per read):
#     T4358 000:106.937 JLINK_ReadMem(0x20000074, 0x2 Bytes, ...)
#     T4358 000:107.203   Data:  0B 01
#   Format B (console-style, single line):
#     200000A8 = AC 00
# Returns hashtable: addr_upper -> byte[] (hex strings)
function Parse-Dump([string]$file) {
    $res = @{}
    if (-not (Test-Path $file)) { return $res }
    $pendingAddr = $null
    foreach ($line in Get-Content $file) {
        # Format A: ReadMem call line - capture upcoming address
        if ($line -match 'JLINK_ReadMem\(0x([0-9A-Fa-f]{8}),\s*0x[0-9A-Fa-f]+\s+Bytes') {
            $pendingAddr = $matches[1].ToUpper()
            continue
        }
        # Format A: matching Data line
        if ($pendingAddr -and ($line -match 'Data:\s+([0-9A-Fa-f]{2}(?:\s+[0-9A-Fa-f]{2})*)')) {
            $bytes = $matches[1].Trim() -split '\s+'
            $res[$pendingAddr] = $bytes
            $pendingAddr = $null
            continue
        }
        # Format B: console-style (in case user redirects stdout)
        if ($line -match '(?:0x)?([0-9A-Fa-f]{8})\s*=\s*([0-9A-Fa-f]{2}(?:\s+[0-9A-Fa-f]{2})*)') {
            $addr = $matches[1].ToUpper()
            $bytes = $matches[2].Trim() -split '\s+'
            $res[$addr] = $bytes
        }
    }
    return $res
}

# Decode one field from parsed dump
function Decode-Field($map, $addr, $size, $sign) {
    if (-not $map.ContainsKey($addr)) { return '????' }
    $bytes = $map[$addr]
    if ($bytes.Count -lt $size) { return '????' }
    # Little-endian
    $v = 0
    for ($i = 0; $i -lt $size; $i++) {
        $b = [int]("0x" + $bytes[$i])
        $v = $v -bor ($b -shl ($i * 8))
    }
    if ($sign -and $size -eq 2 -and $v -ge 0x8000) { $v = $v - 0x10000 }
    if ($size -eq 2) {
        return ('{0,6} (0x{1:X4})' -f $v, ($v -band 0xFFFF))
    } else {
        return ('{0,3} (0x{1:X2})' -f $v, ($v -band 0xFF))
    }
}

# Read all 6 dumps
$dumps = @{}
foreach ($n in 0..5) {
    $dumps["T$n"] = Parse-Dump ("dump_T$n.log")
}

# Build output
$out = New-Object System.Collections.ArrayList
[void]$out.Add('HDICU 5-Issue Joint Diagnostic Summary')
[void]$out.Add('Generated: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
[void]$out.Add('')

$colTitles = @('T0 baseline', 'T1 KEY7 #1', 'T2 KEY7 #2', 'T3 KEY5 long', 'T4 +30s', 'T5 KEY1 x5')
$header = ('{0,-32}' -f 'Field')
foreach ($t in $colTitles) { $header += ('{0,-22}' -f $t) }
[void]$out.Add($header)
[void]$out.Add(('-' * $header.Length))

foreach ($f in $fields) {
    $line = ('{0,-32}' -f $f.Name)
    foreach ($n in 0..5) {
        $val = Decode-Field $dumps["T$n"] $f.Addr $f.Size $f.Sign
        $line += ('{0,-22}' -f $val)
    }
    [void]$out.Add($line)
}

[void]$out.Add('')
[void]$out.Add('=== KEY DIAGNOSIS HINTS ===')
[void]$out.Add('Issue 1 (KEY7 toggles supply-O2 time 0 <-> 8:32):')
[void]$out.Add('  Look at row "control.o2_accumulated_sec" for T0 / T1 / T2.')
[void]$out.Add('  - If toggles 0 <-> 512: confirms RAM aliasing (real bug, must fix in firmware).')
[void]$out.Add('  - If stays constant: screen-side display bug (firmware OK, screen reads wrong byte).')
[void]$out.Add('  Also compare "setpoint.fresh_air" - it should toggle 0->1->0.')
[void]$out.Add('')
[void]$out.Add('Issue 2 (Inner-cycle LED stuck ON):')
[void]$out.Add('  Look at T0 "setpoint.inner_cycle".')
[void]$out.Add('  - = 0 but LED stuck on -> hardware (PA0 short, wiring).')
[void]$out.Add('  - = 1 -> software wrote it (KEY6 bounce or boot path).')
[void]$out.Add('')
[void]$out.Add('Issue 3 (O2 stuck at 61%):')
[void]$out.Add('  Compare T0 vs T4 (30 seconds apart) on "sensor.o2_raw_x10".')
[void]$out.Add('  - Same value -> O2 sensor not reporting; check "sensor.o2_valid" (should be 0).')
[void]$out.Add('  - Different -> sensor live, display lag elsewhere.')
[void]$out.Add('')
[void]$out.Add('Issue 4 (Nursing 5 levels with double LED at 4):')
[void]$out.Add('  T5 "setpoint.nursing_level" after 5 presses:')
[void]$out.Add('  - 1->2->3->1->2 (ends at 2): firmware is 3-level (frozen spec); need new feature.')
[void]$out.Add('')
[void]$out.Add('Issue 5 (open-O2 starts at 25:30, not 0):')
[void]$out.Add('  T3 "control.o2_accumulated_sec":')
[void]$out.Add('  - = 0: only happens on pristine boot; if you saw 25:30, accumulator was carried.')
[void]$out.Add('  - != 0 (e.g. 1530): need to clear o2_accumulated when open-O2 toggles 0->1.')

$out -join "`r`n" | Out-File -Encoding UTF8 'summary.txt'
Write-Host 'summary.txt generated successfully.'
