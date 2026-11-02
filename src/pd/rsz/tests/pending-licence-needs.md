# Pending-licence cases: mock-library feature needs (PR4)

Every adapted suite case plus the gcd command case is
pending-licence: each needs PDK lib/lef files that are NOT
committed (see tests/README.md for the exclusion list).
No case has a CTest entry yet. When the future EOS-owned
mock-library unit ports these cases to EOS-owned data, this
file states exactly what each case needs, so the mock can be
built to cover it. Nothing here is dropped silently: the
scripts and (where the engine runs) the .ok files are
committed alongside, and the mock port re-runs them.

## Shared needs (all cases)

- Liberty: combinational timing arcs (delay tables over
  input slew and output load), sequential arcs for every
  flip-flop (clock-to-output delay, setup and hold
  constraints), pin capacitances, max_transition and
  max_capacitance limits per pin where the case relies on
  them, and drive-strength variants for resizing (the
  engine picks larger/smaller siblings of the same family).
- Insert families the repair passes draw from. Nangate45
  cases: BUF_X*, INV_X*, CLKBUF_X*, LOGIC1_X* (tie).
  sky130 cases: __buf_*, __inv_*, __clkbuf_*, __conb_*
  (tie HI/LO). The mock must provide at least one buffer,
  one inverter, one clock buffer, and one tie cell per
  family used, with distinct drive strengths.
- Tech: routing layers with width, RPERSQ, CPERSQDIST,
  and EDGECAPACITANCE, so the tech-derived wire-RC
  derivation reproduces the scalars in the per-case rows
  (R = RPERSQ / width, C = CPERSQDIST * width +
  2 * EDGECAPACITANCE, with the command's unit
  conversions). A layer with no RC data is also needed
  for the wire3 negative case (any name absent from the
  tech works; the committed script uses metal99).
- SITE/ROW: DEFs with ROW statements name a row site.
  Nangate45-family DEFs use FreePDK45_38x28_10R_NP_162NW_34O;
  the sky130 gcd DEF uses unithd. Generated hi-fanout
  DEFs carry no ROW statements and need no site. The
  mock tech must define whatever site its ported DEFs
  name, or the ported DEFs must drop ROW statements.
- SDC forms used: create_clock (period, optional name),
  set_propagated_clock, set_input_delay, set_output_delay,
  set_max_delay (-ignore_clock_latency, -from pin -to
  pin), set_load (on nets, values 0, 1.0, 50), 
  set_max_transition (on the design), set_max_fanout (on
  the design). All are read through STA read_sdc.
- Wire-RC scalars below are the tech-derived values the
  command reported for each running case (ohm/m, F/m).
  Crash cases have no .ok, so their rows give the layer
  only; the mock run must re-derive the scalar.

## Per-case needs

Legend: PDK = local-only PDK inputs the script references.
DEF = committed DEF file (component count, masters).
Wire = wire layer and (where measured) derived scalar.

- setup1: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_setup1.def, 5 comps (BUF_X1, DFF_X1). Clock clk
  0.3 ns. Setup repair. Wire metal3, 3571428.57 ohm/m,
  5.225615e-11 F/m. Needs: DFF setup/hold arcs, BUF
  drive variants. No ROWs.
- setup2: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_setup2.def, 3 comps (AND2_X1, NAND2_X1, XNOR2_X1).
  SDC clock 0.1 ns, uncertainty 0, input delays 0.02,
  output delays 0.02. Design then setup repair. Wire
  metal3 signal (3571428.57, 5.225615e-11), metal5 clock
  (1500000, 6.66638e-12). ROW site FreePDK45. Needs:
  combinational arcs for all three cells, plus long-wire
  repair: upstream design pass repairs 3 long wires at
  its 853 um default length (EOS passes max_wire 0 and
  skips this; see README cross-check).
- setup3: PDK sky130hd_tt.lib, sky130hd.tlef,
  sky130hd_std_cell.lef. DEF repair_setup3.def, 3 comps
  (buf_1, dfxbp_1, dlxtn_1, inv_1). SDC clock 1 ns,
  set_load 1.0 on l2q. Setup repair. Wire met1,
  892857.143 ohm/m, 8.4742976e-11 F/m. Needs: latch
  (dlxtn) arcs, buf/inv variants. No ROWs.
- hold1: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_hold1.def, 13 comps (AND2_X1, BUF_X1, DFF_X1).
  SDC clock 2 ns, input delays 0, propagated clock.
  Pin hold on r3/D with BUF_X1. Wire metal1,
  5428571.43 ohm/m, 6.013127e-11 F/m. ROW site
  FreePDK45. Needs: DFF hold arcs, BUF_X1 delay.
- hold2: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_hold1.def, 13 comps (same as hold1). Clock 2 ns
  (committed script uses ideal clock; faithful port
  needs propagated clock plus input delays 0 and output
  delay -0.3). Hold repair. Wire metal1 (same scalar
  as hold1). ROW site FreePDK45. Needs: DFF hold arcs
  with clock skew (propagated clock is what creates
  the violations).
- hold3: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_hold3.def, 13 comps (BUF_X1, DFF_X1). Clock 2 ns
  (committed script uses ideal clock; faithful port
  needs propagated clock). Hold repair. Wire metal1.
  No ROWs. Needs: DFF hold arcs with clock skew.
- hold4: PDK sky130hs_tt.lib, sky130hs.tlef,
  sky130hs_std_cell.lef. DEF repair_hold4.def, 15 comps
  (buf_2, dfxbp_1, hs variants). Clock 2 ns (committed
  script uses ideal clock; faithful port needs
  propagated clock). Hold repair. Wire met1,
  892857.143 ohm/m, 8.4742976e-11 F/m. No ROWs. Needs:
  hs DFF hold arcs with clock skew.
- hold5: PDK sky130hs (same three files as hold4). DEF
  repair_hold4.def, 15 comps. SDC clock 2 ns,
  propagated clock, max_delay 0.2 from r1/CLK to r3/D
  (ignore_clock_latency). Hold repair, blocked by
  setup (no buffers). Wire met1 (same scalar as
  hold4). No ROWs. Needs: max_delay constraint
  support, hs DFF arcs.
- hold6: Same as hold5 plus -allow_setup_violations.
  Hold repair inserts 3 buffers upstream.
- hold7: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  gcd_nangate45_placed.def, 571 comps, 44 masters
  (AND2/3/4, AOI21/211/22/221, OAI21/211, NAND2/3/4,
  NOR2/3/4, OR2/3, XOR2, XNOR2, BUF_X1/2/4/8/16,
  INV_X1/2, DFF_X1, MUX2_X1, FILLCELL_X1). Clock clk
  2 ns (upstream names it core_clock). Hold repair
  with slack_margin 0.4 and max_utilization 13.
  Wire metal3. ROW site FreePDK45. Needs: full
  combinational + sequential liberty for the 44
  masters, utilization (core area) support.
- hold8: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_hold1.def, 13 comps. SDC clock 2 ns, input
  delays 0, output delay -0.1, propagated clock. Hold
  repair with slack_margin 0.2. Wire metal1. ROW site
  FreePDK45. Needs: DFF hold arcs with skew and
  output delay.
- hold9: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  gcd_nangate45_placed.def, 571 comps (same masters as
  hold7). Clock 2 ns. Hold repair with slack_margin
  0.4 and max_buffer_percent 2. Wire metal3. ROW site
  FreePDK45. Needs: same liberty as hold7, buffer
  cap support.
- slew1: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_slew1.def (generated hi-fanout), 31 comps
  (DFF_X1). SDC clock 1 ns, max_transition 0.05 on
  the design. Design repair. Wire metal3. No ROWs.
  Needs: DFF pin max_transition behavior, BUF/INV
  variants for resizing.
- slew2: PDK Nangate45_typ.lib, Nangate45.lef, plus
  repair_slew2.lib/lef (local-only: Nangate header).
  DEF repair_slew2.def (generated), 21 comps
  (DFF_MAX_SLEW, DFF_X1). Clock 1 ns. Design repair.
  Wire metal3. No ROWs. Needs: a DFF variant with a
  pin max_transition limit (DFF_MAX_SLEW analog).
- slew3: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_slew3.def, 3 comps (BUF_X1). SDC
  max_transition 0.03 on the design. Design repair.
  Wire metal3. No ROWs. Needs: slow-driver slew
  violation shape (long wire under the length cap).
- slew4: PDK Nangate45_typ.lib, Nangate45.lef, plus
  repair_slew4.lib/lef (committed: lef only; the lib
  carries a Nangate copyright comment and stays
  local-only). DEF
  repair_slew4.def, 4 comps (BUF1, BUF_X1, BUF_X8).
  Design repair, no SDC (liberty pin max_transition
  drives it). Wire metal3. No ROWs. Needs: load pin
  max_transition below driver max_transition.
- slew5: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_slew5.def, 21 comps (BUF_X1, NAND4_X1,
  NOR4_X1). SDC set_load 50 on six nets (u2zn..u7zn),
  max_transition 0.15 on the design. Design repair.
  Wire metal3. No ROWs. Needs: set_load support,
  NAND4/NOR4 arcs.
- slew6: PDK sky130hd_tt.lib, sky130hd.tlef,
  sky130hd_std_cell.lef. DEF repair_slew6.def
  (generated), 51 comps (dfxtp_1, o21ai_0). Design
  repair, no clock. Wire met3, 156666.667 ohm/m,
  8.568987e-11 F/m. No ROWs. Needs: weak-driver
  hi-fanout shape, o21ai arcs.
- slew8: PDK sky130hd (same three files as slew6).
  DEF repair_slew8.def (generated), 250 comps
  (dfrtp_1). Clock 1 ns. Design repair. Wire met2
  signal, met5 clock. dont_use globs for probe cells
  (probe_p_*, probec_p_*; 1 cell each in the full
  liberty). No ROWs. Needs: reset-net hi-fanout
  shape, dont_use glob matching, met2/met5 layer
  data.
- slew9: PDK sky130hd (same three files). DEF
  repair_max_slew_cap.def (generated), 105 comps
  (dfxtp_1, o21ai_0). Clock 1 ns. Design repair
  with max_slew_margin 5. Wire met3. No ROWs. Needs:
  same cells as slew6, slew-margin support.
- cap1: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_cap1.def (generated), 61 comps (DFF_X1).
  Clock 1 ns. Design repair (max_cap). Wire metal3.
  No ROWs. Needs: DFF pin capacitance limits.
- cap2: Same as cap1 except no clock (unconstrained)
  and DEF repair_cap2.def (61 comps, DFF_X1).
- cap3: PDK sky130hd (same three files). DEF
  repair_max_slew_cap.def (generated), 105 comps
  (dfxtp_1, o21ai_0). Clock 1 ns. Design repair
  with max_cap_margin 10. Wire met3. No ROWs. Needs:
  same cells as slew9, cap-margin support.
- fanout1: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_fanout1.def (generated), 36 comps (DFF_X1).
  SDC clock 10 ns, max_fanout 10 on the design.
  Design repair. Wire metal1. No ROWs. Needs:
  max_fanout constraint support, DFF loads.
- fanout2: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_fanout2.def, 16 comps (BUF_X1, DFFR_X1).
  SDC max_fanout 10 on the design, no clock.
  Design repair. Wire default metal1 (upstream ran
  with no wire RC at all). No ROWs. Needs:
  max_fanout support, special-net exclusion (the
  case is about hands-off special nets).
- fanout3: PDK Nangate45_typ.lib, Nangate45.lef, plus
  repair_fanout3.lef (local-only: Nangate header).
  DEF repair_fanout3.def, 13 comps (DFF_X1, LEFONLY).
  SDC max_fanout 10 on the design, no clock.
  Design repair (upstream requests buffer cell
  BUF_X1). Wire default metal1. No ROWs. Needs: a
  LEF-only macro (no liberty) as a load, BUF_X1.
- wire1: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_wire1.def, 3 comps (BUF_X1). SDC set_load 0
  on out1. Design repair with max_wire_length 800 um.
  Wire metal3. No ROWs. Needs: 2000 um wire shape,
  wire-length repair (buffer insertion on long
  wires).
- wire2: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_wire2.def, 4 comps (BUF_X1). SDC set_load 0
  on out1 and out2. Design repair with
  max_wire_length 1000 um. Wire metal3. No ROWs.
  Needs: L-shaped two-load wire shape.
- wire3: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_wire1.def, 3 comps (BUF_X1). Design repair
  with max_wire_length 600 um and a layer with no
  RC data (committed script: metal99). NEGATIVE
  case: the mock tech must let the fail-fast fire
  (named layer, fix text) and must not silently
  supply RC for the named layer.
- wire4: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_wire4.def, 4 comps (BUF_X1). SDC set_load 0
  on out1. Design repair with max_wire_length
  900 um. Wire metal3. No ROWs. Needs: two loads
  driven from the middle.
- wire5: PDK Nangate45_typ.lib, Nangate45.lef, plus
  fakeram45_64x32.lib/lef and pad.lib/lef
  (local-only). DEF repair_wire5.def (RocketTile),
  2 comps (BUF_X1, PAD). Design repair with
  max_wire_length 600 um. Wire metal3. ROW site
  FreePDK45. Needs: PAD macro, long wire to a pad,
  multi-lib/lef load (3 of each).
- wire6: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_wire1.def, 3 comps (BUF_X1). Design repair
  with max_wire_length 600 um, no SDC. Wire metal3.
  No ROWs. Needs: 1500 um wire to an output port.
- wire7: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_wire7.def, 3 comps (BUF_X1). Design repair
  with max_wire_length 600 um, no SDC. Wire metal3.
  No ROWs. Needs: 1500 um wire from an input port.
- tie1: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_tie1.def, 6 comps (BUF_X1, LOGIC1_X1). Tie
  repair on LOGIC1_X1/Z. Wire default metal1. No
  ROWs. Needs: tie cell with output Z, tie-fanout
  placement (2 connections per tie net).
- tie2: Same as tie1 plus -separation 2 um (tie to
  load distance). DEF repair_tie2.def, 6 comps.
- tie3: PDK Nangate45_typ.lib, Nangate45.lef, plus
  pad.lef (committed: clean; pad.lib intentionally
  NOT loaded). DEF repair_tie3.def, 8 comps
  (BUF_X1, LOGIC1_X1, PAD). Tie repair on
  LOGIC1_X1/Z. Wire default metal1. No ROWs. Needs:
  non-liberty pin on the net (PAD without liberty).
- tie4: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_tie4.def (generated), 11 comps (BUF_X1,
  LOGIC1_X1). SDC max_fanout 5 on the design.
  Design then tie repair on LOGIC1_X1/Z (design
  pass must not repair the tie net). Wire default
  metal1. No ROWs. Needs: hi-fanout tie shape (10
  loads), max_fanout support.
- tie5: PDK Nangate45_typ.lib, Nangate45.lef, plus
  pad.lef (committed; pad.lib not loaded). DEF
  repair_tie5.def, 5 comps (BUF_X1, LOGIC1_X1, PAD).
  Tie repair on LOGIC1_X1/Z. Wire default metal1.
  ROW site FreePDK45. Needs: tie to a pad outside
  the core.
- tie6: PDK Nangate45_typ.lib, Nangate45.lef. DEF
  repair_tie6.def, 2 comps (BUF_X1, LOGIC1_X1). Tie
  repair on LOGIC1_X1/Z. Wire default metal1. No
  ROWs. Needs: tie net driving an output port
  (2 connections counting the port pin).
- tie7: PDK Nangate45_typ.lib, Nangate45.lef, plus
  repair_tie7.lib (local-only: Nangate header) and
  repair_tie7.lef (committed: clean). DEF
  repair_tie7.def, 6 comps (BUF_X1, TIE_X1). Tie
  repair on TIE_X1/Z0 then TIE_X1/Z1 (repeatable
  port option). Wire default metal1. No ROWs.
  Needs: dual-output tie cell.
- clk_nets1: PDK Nangate45_typ.lib, Nangate45.lef.
  DEF repair_wire1.def, 3 comps (BUF_X1). Clock
  in1, 10 ns. Clock-nets repair with
  max_wire_length 600 um, clock nets in1 and n2.
  Wire metal3. No ROWs. Needs: clock-net marking
  (setSigType CLOCK), clock wire repair.
- clk_inverters1: PDK Nangate45_typ.lib, Nangate45.lef.
  DEF repair_clk_inverters1.def, 6 comps (AND2_X1,
  BUF_X1, DFF_X1, INV_X1). Clock clk1, 1 ns.
  Clock-inverters repair, clock net clk1. Wire
  default metal1 (upstream ran with no wire RC).
  No ROWs. Needs: clock inverter to clock buffer
  conversion shape.
- gcd: PDK sky130 tlef (sky130_fd_sc_hd.tlef), merged
  lef, tt liberty (local-only ORFS files). DEF
  gcd_6_final.def (routed), 1046 comps, 70 masters
  (a2111oi/a21oi/a21o/and2/and2b/clkbuf/clkinvlp/
  dfxtp/dlygate/fill/inv/maj3/mux2i/nand2/nand3/
  nand4/nor2/nor3/nor4/o2111a/o21a/o21ai/o221ai/
  o311ai/or2b/or3/tapvpwrvgnd/xnor2/xor2 families,
  drives 0/1/2/4/8). SDC degraded: core_clock
  2.9 ns, propagated clock, input delays 0.68 on
  req_msg ports, output delays 0.68 on resp_msg
  ports. Setup then hold (command default). Wire
  met2, 892857.143 ohm/m, 7.7889922e-11 F/m.
  ROW site unithd. Needs: full routed-design
  liberty for the 70 masters, routed-wire timing,
  tap/fill cells.
