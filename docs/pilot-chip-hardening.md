# Pilot chip: gcd in the Tiny Tapeout harness, hardened locally

Phase 3, unit 3.4. Route decision record: `docs/submission-route-decision.md`
(Route A, Tiny Tapeout Sky130, target run TTSKY26d). This document is the
evidence record for the pilot chip: a serializer wrapper around the gcd
design, assembled in the Tiny Tapeout harness, functionally verified, and
hardened with the route's own flow, all locally. EOS-side deliverables are
evidence documents only; the pilot project itself is a local workspace
artifact and is not part of this repository.

## 1. Scope and standing gates

Everything in this unit was built and run locally. The pilot project has
no git remote and was never pushed anywhere. Nothing was submitted in
the Tiny Tapeout app, no tile or devkit was purchased, no terms were
accepted, and no licence was committed for the pilot sources (the
wrapper source deliberately carries no licence header). Items 1, 8, 9
and 10 of the checklist in Section 3.3 of the route memo (public
repository, enabling the GDS action on it, submitting in the app,
buying a devkit) were not performed; they remain gated on a separate
decision. TTSKY26d closes 2026-11-30.

## 2. Pilot project provenance

| Item | Pin |
|---|---|
| Template | TinyTapeout/ttsky-verilog-template at `a42e6662fab316e93b4e26399e21d41664a460b7` |
| GDS action (per the template's `gds.yaml`) | TinyTapeout/tt-gds-action at tag `ttsky26d`, commit `e013b994dc124e6bbce5e4ae54d93908ac98141e` |
| GDS action inputs used | `librelane-version` default 3.0.14, `pdk` sky130A |
| tt-support-tools | local clone at `01d5d2814fa9dd61e9d211e0b235a4a592a9316a` (the action checks out ref `main`, unpinned; this is the checkout the local runs used) |
| Pilot sources | local git repository, no remotes, HEAD `2cfda968bdd24137d9ee0111864131d52d284276` |
| gcd source | ORFS `flow/designs/src/gcd/gcd.v`, SHA-256 `5009f224876d39e5e77e59ee1528cb0d2a03697251f0971b77cf3e8b0426dd3e`, copied unmodified, then one local patch (below) |
| Wrapper source | `src/project.v`, new, SHA-256 `be89010af5e12e9a5cc04fdcb1e6921b8177d1410b7d7d077ef9fbce2ce9e5f2` |

The gcd patch, disclosed: the PyMTL-generated control block assigns
`do_swap` and `do_sub` only inside its CALC state branch. LibreLane's
lint step runs Verilator with latch warnings as errors, so the pilot
copy adds default assignments (`do_swap = 0; do_sub = 0;`) at the top of
that combinational block, with an in-source comment naming it as a
local pilot patch. The change is functionally inert: both signals are
read only inside the CALC branch, after assignment. The unified diff
against the ORFS original is one hunk, 18 lines. No other change was
made to gcd.v, and nothing in upstream collateral was patched.

One configuration change in `src/config.json`, also disclosed:
`LINTER_DISABLE_WARNINGS` is set to `["DECLFILENAME", "EOFNEWLINE",
"BADVLTPRAGMA"]`. gcd.v carries ten `// verilator-xinit: zeros`
annotation comments that Verilator 5.x misparses as malformed lint
pragmas, producing ten fatal BADVLTPRAGMA errors at the lint step. The
LibreLane variable replaces (does not extend) the default warning
exemption list, hence all three names. After this change and the latch
patch, lint completes with 0 errors and 14 warnings, all
UNUSEDSIGNAL or UNUSEDPARAM inside gcd.v submodule internals.

## 3. The serializer wrapper

Top module `tt_um_mtmoreira_gcd` wraps gcd's native interface (clk,
reset, req_val, req_rdy, req_msg[31:0], resp_val, resp_rdy,
resp_msg[15:0]) onto the tt_um_ interface. The protocol, documented in
the pilot's `docs/info.md`:

- `ui_in[7:0]` is the input byte bus. The 32-bit request is loaded as
  four bytes, most significant first: a[15:8], a[7:0], b[15:8], b[7:0].
  Each byte is captured on a rising edge of WR_STB (`uio[0]`, input).
  gcd's request message is {a[15:0], b[15:0]}.
- While loading, `uo_out[7:0]` echoes the count of bytes accepted so
  far (0 to 3). It reads 0 while the core computes.
- When the response is ready, `uo_out[7:0]` presents the result low
  byte first. A rising edge of RD_STB (`uio[1]`, input) advances to the
  high byte; a second read edge ends the transaction and re-arms the
  loader.
- Status on the bidirectional pins (outputs, `uio_oe` = 8'b1111_0000):
  READY (`uio[4]`, accepting bytes), BUSY (`uio[5]`), DONE (`uio[6]`,
  response waiting), PHASE (`uio[7]`, 0 = low byte presented, 1 = high
  byte). `uio[2]` and `uio[3]` are unused inputs.
- The wrapper translates reset polarity (the harness `rst_n` is active
  low, gcd's reset is active high and synchronous) and holds
  resp_rdy asserted until the response is captured.

## 4. Functional verification

The cocotb suite in the pilot's `test/` drives the documented protocol
for eleven operand pairs, including (1071, 462), (65535, 255),
(32768, 24576), (0, 7), (7, 0) and (13, 13), checking the two response
bytes, the load-progress echo, and the READY, BUSY, DONE and PHASE
sequencing against Python's `math.gcd`.

- RTL simulation (Icarus Verilog, cocotb 2.1.0): PASS, TESTS=1 PASS=1
  FAIL=0 SKIP=0. The suite passes identically before and after the
  gcd latch patch (same simulated time, 60,600 ns), which is the
  functional evidence that the patch is inert.
- Gate-level simulation (the gl_test action's local equivalent): the
  same suite against the hardened netlist from the submission package,
  compiled with the sky130_fd_sc_hd Verilog models from the hardening
  PDK and Icarus Verilog 13.0 (the version the action installs):
  PASS, TESTS=1 PASS=1 FAIL=0 SKIP=0, and results.xml contains no
  failure or error element, which is the pass criterion the action
  itself applies at tag `ttsky26d`.

## 5. Hardening run

Method. The GDS action hardens with tt_tool's `--harden`, which invokes
LibreLane on `src/config_merged.json` (the project's config merged with
the Tiny Tapeout flow configuration). The local run executed exactly
that invocation, with the tools from the official LibreLane 3.0.14
devshell AppImage (the action's own distribution channel for this
version): file size 1,398,041,120 bytes, SHA-256
`2ed4526bb8c70d373974d872914d96f65e0276ba18abc581a8de2c257394ca33`.
Pinned tools inside it: OpenROAD (build 2026-02-17,
`dcf36133a369abc8f3c5e5738cd4d82e4903c0e0`), Yosys 0.62
(`7326bb7d6641500ecb285c291a54a662cb1e76cf`, with plugins), Verilator
5.044, Magic 8.3.623, KLayout 0.30.7, Netgen 1.5.316.

PDK. LibreLane 3.0.14 pins the sky130 ciel (open_pdks) build
`8afc8346a57fe1ab7934ba5a6056ea8b43078e71`; that is the PDK the flow
ran against. Two disclosures about local mechanics: (a) ciel's own
download client timed out repeatedly through this sandbox's HTTP proxy
(its HTTP library's 5 second default against a manifest fetch measured
at 5.4 to 5.5 s), so the release was fetched by replicating ciel's
fetch and unpack algorithm over a working transport, from ciel's
publisher manifest, unmodified; (b) ciel releases ship no
`sky130A/SOURCES` file, which is what tt_tool's PDK version reader
parses after a flow, so the run's `pdk.json` and `commit_id.json`
records were written by hand in tt_tool's exact schema
(FLOW_NAME LibreLane, FLOW_VERSION 3.0.14, PDK sky130A, PDK_SOURCE
open_pdks, PDK_VERSION `8afc8346a57fe1ab7934ba5a6056ea8b43078e71`;
commit record naming the pilot HEAD above and repo "N/A", tt_tool's
own value for a repository without remotes).

The flow was run twice end to end from a clean run directory. Every
metric quoted below was identical in both runs.

Configuration: die 0 0 161.00 111.52 (the 1x1 tile of
`tt_block_1x1_pg.def`), flow clock period 20 ns. Note that
`info.yaml` documents `clock_hz` 10,000,000 (the datasheet figure);
the template flow constrains hardening at 20 ns regardless, and the
timing results below are at that tighter period.

### Quality of results (final metrics of the run)

| Metric | Value |
|---|---|
| Flow errors | 0 |
| Die area | 17,954.7 um2 (161.00 x 111.52 um) |
| Core area | 16,493.28 um2 |
| Standard cell area | 8,399.31 um2 |
| Instance utilization (metrics) | 0.5093 |
| tt_tool routing summary | utilisation 41.043 %, wire length 16,665 um |
| Instances, total | 3,237 (659 logic and other cells, 225 tap, 2,353 fill) |
| Synthesis chip area (Yosys) | 5,885.64 um2, 48.81 % sequential |
| Setup worst slack, 9 corners | +7.7016 ns (max_ss_100C_1v60, 20 ns period) |
| Hold worst slack, 9 corners | +0.1036 ns |
| Setup or hold violations | 0 in all corners |
| Total power (nom) | 5.196e-4 W (internal 2.326e-4, switching 2.635e-4, leakage 2.72e-8) |
| Final route DRC errors | 0 |
| Total vias | 4,452 |

### Physical verification inside the flow

Magic DRC: 0 errors. LVS (Netgen, extracted layout against the
post-synthesis netlist): clean, zero on every difference and unmatched
counter the flow records. Antenna: 0 violating nets and 0 violating
pins, in the checker and in routing. Power grid: 0 violations on VPWR
and VGND. KLayout DRC coverage comes from the precheck suite in
Section 6 (the Classic flow itself uses KLayout for stream-out and
rendering, not for a DRC step).

### Design-rule violations in the raw STA metrics, named

The flow's gated checkers pass, but the raw per-corner STA metrics are
not all zero, and they are reported here rather than rounded away:

- Max slew: 167 violations at ss_100C_1v60 (max and nom), 11 at
  tt_025C_1v80, 0 at ff. The violating pins sit on the reset
  distribution net driven from `rst_n` (slew 1.196 ns against a
  0.75 ns limit at ss; 0.761 ns at tt). LibreLane's sky130
  configuration sets the max slew and max cap checkers to match no
  corners (`corner_override = [""]`), so the flow does not gate on
  them by design; max cap is 0 in every corner in any case.
- Max fanout: 1 violation in every corner, the clock root buffer
  output (`clkbuf_0_clk/X`, fanout 16 against a limit of 10). No
  gating checker covers fanout in this flow.

Setup and hold timing, the quantities the flow does gate, are clean
in all nine corners with the margins in the table above.

### Artifacts

SHA-256 of the submission package contents (identical GDS hash in the
run's final views, as expected for a copy):

| Artifact | SHA-256 |
|---|---|
| tt_um_mtmoreira_gcd.gds (1,595,916 bytes) | `4e3a47fd03050533d95070834860d43ab62096ee57acc51dde96b9d3bbcc8d3f` |
| tt_um_mtmoreira_gcd.oas | `345e8fd5bfd65bfb0717aea2fad71eba10eff973813273fbc6ee0901be9387da` |
| tt_um_mtmoreira_gcd.lef | `92d167f731e7d50c7d51fcb19bae9a8c10f27014779c718898a19d6d36498b52` |
| Gate-level netlist (.v) | `fea1befc92a3e3fca6c87a9ea89e487b6b9c84cb9af3365fa30fa70d3a8028c3` |
| Nom-corner SPEF | `43f8ec3dd8e73af2aa788f295000be980e18f29cd443fe53c29be0fc268f7644` |
| Final metrics.json | `3d0b8d0fd5b8c2c07b0044dd93a49ace76f7f9003f363a880830cda4d159cd2b` |
| datasheet.pdf (41,451 bytes) | `98cc24e50e141e808152b240ca79e9e3b658defbda07c01ba21988563440e623` |

The tt_tool post-flow steps the action runs were also executed
locally, all exit 0: `--print-warnings` (no warnings listed),
`--print-stats`, `--print-cell-category` (659 cells excluding fill
and tap, agreeing with the instance counts above),
`--create-tt-submission` and `--create-png`.

## 6. Tiny Tapeout precheck, run locally

The precheck action (in tt-gds-action at `ttsky26d`) runs
tt-support-tools' `precheck.py` against the submission GDS under a nix
shell, with PDK_VERSION for sky130A pinned in the action at
`0536d02d875c8f67dd7cca3902ac457e62f20005`. Note this is a different
open_pdks build from the hardening PDK; that is how the route's own
actions pin them, and each local run used its own action's pin. The
local run executed `precheck.py` (tt-support-tools checkout above)
against that PDK build, with the precheck's own pinned Python
dependencies (klayout module 0.30.8, gdstk 0.9.52, yowasp-yosys 0.63)
and Magic 8.3.623 and KLayout 0.30.7 from the LibreLane devshell; the
action's nix environment pins Magic 8.3.568 and KLayout 0.30.4
(tool-versions.json), so the local tool versions are close to but not
identical with the action's, which is recorded here rather than
elided.

Result: **precheck passed, all 15 checks for sky130A green**: Magic
DRC; KLayout FEOL; KLayout BEOL; KLayout offgrid; KLayout pin label
overlapping drawing; KLayout zero area; KLayout Checks; pin check;
boundary check; power pin check; layer check; cell name check;
urpm/nwell check; analog pin check; Verilog syntax check.

## 7. Documentation checks

The docs action's checks were run locally through tt_tool:
`--check-docs` passed, and `--create-pdf` produced the pilot
datasheet (hash in Section 5) using typst 0.13.1, the version the
docs action installs.

## 8. Checklist status (route memo, Section 3.3)

| # | Item | Status after unit 3.4 |
|---|---|---|
| 1 | Create the public project repository from the template | Not performed. Gated (publication). The template was instantiated locally instead. |
| 2 | Enable the workflows / GDS action, push code and GDS | Not performed as stated. Gated (publication). Local equivalents of every action the workflow runs are evidenced in Sections 4 to 7. |
| 3 | Sources in `src/`, top module matches | Done locally: `project.v` + `gcd.v`, top `tt_um_mtmoreira_gcd`. |
| 4 | `info.yaml` filled (title, author, language, clock, tiles, top module, sources, pin labels) | Done locally. |
| 5 | Testbench in `test/` passes | Done locally, RTL and gate level (Section 4). |
| 6 | Records from the GDS action summary | Local equivalent: run metrics, tt_tool summaries and artifact hashes (Section 5). |
| 7 | Records from the precheck action | Local equivalent: 15 of 15 checks pass (Section 6). |
| 8 | Submit in the app (`app.tinytapeout.com`) | Not performed. Gated (spend; also commits the project). |
| 9 | Order the tile | Not performed. Gated (spend, explicitly out of scope). |
| 10 | Optionally buy a devkit | Not performed. Gated (spend). |

## 9. Ownership

| Layer | Owner | Contents |
|---|---|---|
| Chip frame | Tiny Tapeout | Pads and the sky130_ef_io_gpiov2_pad IO cells, power entry, the multiplexer that shares the frame between projects, demo boards |
| Tile | The project | The serializer wrapper and gcd, its constraints and configuration, its tests and documentation |
| Evidence chain | EOS | The Sky130 package model, the PV and correlation evidence of units 3.1 and 3.2, and the mapping of that evidence to this checklist |

## 10. A correction to the unit 3.3 memo

The route decision memo (Section 2.3) says gcd's interface totals 55
signal bits. The correct count is 54: clk 1, reset 1, req_val 1,
req_rdy 1, req_msg 32, resp_val 1, resp_rdy 1, resp_msg 16. The
memo's own pin list sums to 54, and the unit 3.2 SPEF's `*PORTS`
section enumerates 54 ports (counted directly from the ORFS result
file). It is a one-off arithmetic slip in the memo's prose; the
wrapper design in this unit was built against the pin list, not the
total, and is unaffected.

## 11. What remains

Unit 3.5 assembles the Phase 3 exit report against the plan's Section
17 checklist. The gated remainder (checklist items 1, 8, 9, 10) stays
exactly as recorded in Section 8: documented, not performed, awaiting
a separate decision, with TTSKY26d closing 2026-11-30.
