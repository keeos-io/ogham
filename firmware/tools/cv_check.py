"""Exercises the CV path on channel A with a real voltage.

The pot is expected to stay put; this triggers on ADC4 (the pot+CV sum) moving.
Reports whether the parameter tracks CV monotonically, how far it swings, and
what it reads when the CV is at rest -- which should equal pot / POT_FULL_SCALE.
"""
import struct, sys, time
import ogham_monitor as m

F = ["magic","counter","segs","modeState","ioState","out1","out2","paramA",
     "paramB","rate","extClockRate","potA","potB","potRate","potLevel",
     "combinedA","combinedB","cvA","cvB","cpuPeak","cpuPeriod","syncCount"]
POT_FULL_SCALE, OFFSET_A, GAIN_A = 0.9450, 0.7501, 0.9990
MOVE, QUIET, MAXW = 0.004, 6.0, 600
# A free-running LFO never goes quiet, so waiting for stillness waits forever --
# which is exactly how the first run of this hung. Once triggered, capture a
# fixed window and stop whether or not the CV settles.
RUN_SECS = 30

r = m.SwdReader()
if not r.start():
    print("CONNECT FAILED"); sys.exit(1)
rows, started, last, first, t0, ref = [], False, None, None, time.time(), None
try:
    addr = m.find_telemetry_addr()
    while True:
        w = r.read_words(addr, m.STRUCT_WORDS); now = time.time()
        if len(w) == m.STRUCT_WORDS:
            d = dict(zip(F, struct.unpack(m.STRUCT_FMT,
                     b"".join(struct.pack("<I", x) for x in w))))
            if d["magic"] == 0x4F474841:
                if ref is None: ref = d["cvA"]
                elif abs(d["cvA"] - ref) > MOVE:
                    if not started: first = now
                    started, last, ref = True, now, d["cvA"]
                if started:
                    rows.append((d["potA"], d["cvA"], d["combinedA"], d["paramA"]))
        if started and (now - last > QUIET or now - first > RUN_SECS): break
        if not started and now - t0 > MAXW: print("no CV movement seen"); sys.exit(1)
        time.sleep(0.05)
finally:
    r.stop()

pot = [x[0] for x in rows]; adc = [x[1] for x in rows]
comb = [x[2] for x in rows]; val = [x[3] for x in rows]
print(f"captured {len(rows)} samples\n")
print(f"  pot A held at        {min(pot):.4f} .. {max(pot):.4f}  (span {max(pot)-min(pot):.4f})")
print(f"  sum ADC4 swung       {min(adc):.4f} .. {max(adc):.4f}")
print(f"  param A swung        {min(val)} .. {max(val)}   ({max(val)-min(val)} counts)")
print()
potmid = sum(pot)/len(pot)
print(f"  pot alone would give {255*potmid/POT_FULL_SCALE:.1f}")
print(f"  expected sum at rest {OFFSET_A - GAIN_A*potmid:.4f}")
print()
# monotonicity: as the sum falls (more CV), the parameter should rise
pairs = sorted(zip(adc, val))
inv = sum(1 for i in range(1, len(pairs)) if pairs[i][1] > pairs[i-1][1])
print(f"  ADC down -> param up in {100*(1-inv/max(len(pairs)-1,1)):.0f}% of adjacent pairs"
      f"  ({'monotonic, correct polarity' if inv < 0.1*len(pairs) else 'CHECK POLARITY'})")
railed = sum(1 for a in adc if a <= 0.0005)
print(f"  samples with the sum railed at 0: {railed} ({100*railed/len(adc):.0f}%)")
