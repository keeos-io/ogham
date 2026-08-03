# JLCPCB Upload Checklist — Ogham v1.0 (ogham-merged-v1.0)

Files: `ogham-merged-v1.0.zip` (gerbers) · `bom.csv` · `positions.csv`
Board: 50 × 110 mm, 2-layer. Assembly: **double-sided SMT** (55 parts bottom / 35 top) + hand-soldered THT.

---

## 1. Upload files
- [ ] Upload gerber zip → confirm JLC preview shows both copper layers, silk, and the board outline (50 × 110 mm)
- [ ] Turn ON "PCB Assembly" → upload `bom.csv` and `positions.csv`
- [ ] Assembly side = **both / top+bottom** (SMD is on both layers)

## 2. Mark "Do Not Place" (DNP) — 14 BOM rows / 19 designators
These are hand-soldered; they have no LCSC (or a non-LCSC MPN) and JLC will ask you to resolve them. Mark each **Do Not Place**:

- [ ] **C5** — 10µF tantalum (MPN `TAP106J016SCS`, not an LCSC number)
- [ ] **A1** — Daisy Seed module
- [ ] **J1** — Eurorack power header
- [ ] **J_CLK1, J_CVA1, J_CVB1, J_GATE_IN1** — 4× audio jacks
- [ ] **J_CV_OUT1** — jack
- [ ] **J_GATE_OUT1** — jack
- [ ] **J_OUT1, J_OUT2** — 2× jacks
- [ ] **J_DISP1** — TM1637 display header
- [ ] **J_DISP_SUPP** — display support header
- [ ] **RVA1, RVB1, RVLEVEL1, RVRATE1** — 4× pots
- [ ] **SW_ENC1** — encoder
- [ ] **SW_MODE1** — mode toggle

_(Everything else — the 90 SMD parts — has a valid LCSC and should stay "Place".)_

## 3. Verify part rotations/polarity in JLC's placement preview
Rotation is the classic assembly error (KiCad vs JLC reference orientation). Eyeball the polarized parts in the online preview; fix any that look flipped 90°/180° before confirming:

- [ ] **Diode cathodes** — D1, D2, **D8** (SS14); D3 (1N4148); D4, D5, D_OCT1, D_OCT2 (BAT54WS); D6, D7 (1N5819WS)
- [ ] **IC pin-1** — U1 (LM4040), U2 (L7805), U3 (MCP6004), U4, U7 (TL072), U5 (74AHCT1G125), U6 (LM393)
- [ ] **Q1** (MMBT3904) collector/emitter orientation
- [ ] Spot-check a couple of the SMD parts on the **top** layer render (double-sided — confirm they're on the correct side)

## 4. Parts / stock confirmation
- [ ] **U1 LM4040 (C156302)** — confirm in stock at order time (was ~370 in JLC library; 1 per board)
- [ ] Note any **Extended parts** loading fee (TL072, MCP6004, LM4040, LM393, 74AHCT1G125 are likely Extended, ~$3 one-time each)

## 5. Place order
- [ ] Review price (double-sided SMT costs more than single-sided)
- [ ] Confirm and order

---

### Hand-solder yourself after boards arrive (the DNP parts above)
8× Thonkiconn jacks · 4× Alpha pots · encoder · mode toggle · Eurorack power header · 2× display headers · C5 tantalum · Daisy Seed (on its pin-header standoffs).
