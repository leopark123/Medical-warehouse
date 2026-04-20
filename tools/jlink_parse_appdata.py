# -*- coding: utf-8 -*-
"""Parse appdata.bin + flash_cfg.bin dumped via JLink.
Empirically verified layout (1-byte enums):
  Sensor     offset  0-29  (30)
  Setpoints         30-47  (18)
  ControlState      48-67  (20)   -- 1-byte enums + padding
  AlarmState        68-71  ( 4)
  SystemState       72-79  ( 8)
  Calibration       80-87  ( 8)
  FactoryLimits     88-105 (18)
  CancelFlags       106-109( 4)
"""
import struct, sys, os, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

RAM   = r"C:\hdicu_dump\appdata.bin"
FLASH = r"C:\hdicu_dump\flash_cfg.bin"

if not os.path.exists(RAM):
    print("ERROR: %s not found. Run jlink_dump.jlink first." % RAM); sys.exit(1)
with open(RAM,"rb") as f: d = f.read()

# Sensor
t = struct.unpack_from("<4h", d, 0)
tavg = struct.unpack_from("<h", d, 8)[0]
hum = d[10]; hum_raw = struct.unpack_from("<H", d, 12)[0]
o2p = d[14]; o2_raw = struct.unpack_from("<H", d, 16)[0]
co2 = struct.unpack_from("<H", d, 18)[0]
hr, spo2 = d[20], d[21]
co2v, o2v, jv = d[22], d[23], d[24]
liq, urine = d[25], d[26]
o2m, o2r = d[27], d[28]

# Setpoints
(tgt_t, tgt_h, tgt_o2, tgt_co2, fog_t, dis_t) = struct.unpack_from("<6H", d, 30)
fan, nur, inner, fresh, opn, light = d[42:48]

# ControlState (1-byte enums, 18B total: 48-65)
ts, hs, os_ = d[48], d[49], d[50]
# pad byte 51
(fog_r, dis_r, o2_acc, relay) = struct.unpack_from("<4H", d, 52)
light_st, sw_st = d[60], d[61]
tbr, tbc, fan_act, nur_act = d[62], d[63], d[64], d[65]

# AlarmState at 66 (4B: 66-69) — FIX: was incorrectly at 68 before
alarm = struct.unpack_from("<H", d, 66)[0]
buzz, ack = d[68], d[69]

# pad 70-71 (4-byte align for uint32)

# SystemState at 72
(runtime_min, uptime) = struct.unpack_from("<2I", d, 72)

# Calibration at 80
cal = struct.unpack_from("<4h", d, 80)

# FactoryLimits at 88
lim = struct.unpack_from("<9H", d, 88)

# CancelFlags at 106
cf = d[106:110]

ts_n = {0:"IDLE",1:"COOLING",2:"HEATING"}.get(ts, "?%d"%ts)
hs_n = {0:"IDLE",1:"HUMIDIFY",2:"DEHUMIDIFY"}.get(hs, "?%d"%hs)
os_n = {0:"IDLE",1:"SUPPLYING",2:"OPEN_MODE"}.get(os_, "?%d"%os_)

def tfmt(x): return "%.1f℃"%(x/10.0) if x!=-999 else "INV"

print("="*72)
print("HDICU MainBoard  g_app_data @ 0x20000138   (120 bytes dumped)")
print("="*72)
print("\n[Sensor 传感器]")
print("  temp[0..3]   : %s" % ", ".join(tfmt(x) for x in t))
print("  temp_avg     : %s       (屏幕/iPad显示+控制用)" % tfmt(tavg))
print("  humidity     : %d%%  raw=%.1f%%" % (hum, hum_raw/10.0))
print("  O2           : %d%%  raw=%.1f%%  valid=%d" % (o2p, o2_raw/10.0, o2v))
print("  CO2          : %d ppm  valid=%d" % (co2, co2v))
print("  HR / SpO2    : %d bpm / %d%%  valid=%d" % (hr, spo2, jv))
print("  液位 / 尿检  : liquid=%d urine=%d  (1=正常)" % (liq, urine))
print("  外部O2请求   : PD8总=%d PB6=%d  (1=active)" % (o2m, o2r))

print("\n[Setpoints 设定]")
print("  目标         : temp=%.1f℃  humid=%.1f%%  o2=%.1f%%  co2=%dppm" %
      (tgt_t/10, tgt_h/10, tgt_o2/10, tgt_co2))
print("  定时         : fog=%ds  disinfect=%ds" % (fog_t, dis_t))
print("  fan / nursing: %d / %d" % (fan, nur))
print("  inner_cycle  : %d   (CN4→PE7电磁铁)" % inner)
print("  fresh_air    : %d   (CN6)" % fresh)
print("  open_o2      : %d   (CN2长按2s→U16阀)" % opn)
print("  light_ctrl   : 0x%02X  检查=%d 照明=%d 蓝=%d 红=%d" %
      (light, light&1, (light>>1)&1, (light>>2)&1, (light>>3)&1))

print("\n[ControlState 实际状态]")
print("  temp_state   : %s       (基于 temp_avg vs target_temp)" % ts_n)
print("  humid_state  : %s" % hs_n)
print("  o2_state     : %s" % os_n)
print("  定时剩余     : fog=%ds  disinfect=%ds  o2_accum=%ds" %
      (fog_r, dis_r, o2_acc))
print("  relay_status : 0x%04X" % relay)
print("  light实际    : 0x%02X  switch实际=0x%02X (内%d 新风%d 开O2%d)" %
      (light_st, sw_st, sw_st&1, (sw_st>>1)&1, (sw_st>>2)&1))
print("  beep timer   : req=0x%02X counter=%d" % (tbr, tbc))
print("  fan/nursing 实际: %d / %d" % (fan_act, nur_act))

print("\n[Alarm] flags=0x%04X buzzer=%d ack=%d" % (alarm, buzz, ack))
if alarm:
    alarm_bits = []
    for bit, name in [(0,"TEMP_HI"),(1,"TEMP_LO"),(2,"HUMID"),(3,"O2_CO2"),
                      (4,"HEART_RATE"),(5,"SPO2_LOW"),(6,"COMM_FAULT")]:
        if alarm & (1<<bit): alarm_bits.append(name)
    print("  触发中: %s" % ", ".join(alarm_bits))

print("\n[System] runtime_min=%d 分钟  boot_uptime=%d s (%.1f min)" %
      (runtime_min, uptime, uptime/60.0))

print("\n[Calibration 校准值 v2.1]  (iPad 0x03 byte22-29写入)")
print("  temp=%+.1f  humid=%+.1f  o2=%+.1f  co2=%+d" %
      (cal[0]/10, cal[1]/10, cal[2]/10, cal[3]))

print("\n[FactoryLimits 出厂限值 v2.1]  (iPad 0x09写入, 动态OOB基准)")
print("  temp : [%.1f, %.1f] ℃" % (lim[1]/10, lim[0]/10))
print("  humid: [%.1f, %.1f] %%" % (lim[3]/10, lim[2]/10))
print("  o2   : [%.1f, %.1f] %%" % (lim[5]/10, lim[4]/10))
print("  uv=%ds  infrared=%ds(reserved)  fog=%ds" % (lim[6], lim[7], lim[8]))

print("\n[CancelFlags 取消标志]  (会话状态,不持久)")
print("  temp=%d humid=%d o2=%d co2=%d   (1=接受, 0=忽略iPad下发)" %
      (cf[0], cf[1], cf[2], cf[3]))

# Flash config region
if os.path.exists(FLASH):
    with open(FLASH,"rb") as f: fd = f.read()
    print("\n" + "="*72)
    print("Flash Config Region A @ 0x0807E000  (前36字节)")
    print("="*72)
    if all(b==0xFF for b in fd[:36]):
        print("  STATUS: 未写入(空白0xFF) → 开机加载硬编码默认值")
        print("  表现: iPad未下发过0x09或0x03(校准), 或做过0x0B恢复出厂")
    else:
        version = struct.unpack_from("<H", fd, 0)[0]
        fc = struct.unpack_from("<4h", fd, 4)
        fl = struct.unpack_from("<9H", fd, 12)
        chksum = struct.unpack_from("<I", fd, 32)[0]
        print("  version     = 0x%04X" % version)
        print("  calibration : temp=%+.1f humid=%+.1f o2=%+.1f co2=%+d" %
              (fc[0]/10, fc[1]/10, fc[2]/10, fc[3]))
        print("  limits      : temp[%.1f,%.1f] humid[%.1f,%.1f] o2[%.1f,%.1f]" %
              (fl[1]/10, fl[0]/10, fl[3]/10, fl[2]/10, fl[5]/10, fl[4]/10))
        print("  timer caps  : uv=%d ir=%d fog=%d" % (fl[6], fl[7], fl[8]))
        print("  checksum    = 0x%08X" % chksum)
