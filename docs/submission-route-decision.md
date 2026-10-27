# Submission route decision memo (unit 3.3)

Date: 2026-10-26
Status: unit 3.3 deliverable. This is a decision record, not a
decision. It compares the open Sky130 submission routes that
were verifiable live on 2026-10-26, states each route's exact
submission checklist with the evidence each item demands, and
ends with a recommendation. The route choice is Matheus's: it
fixes the checklist the Phase 3 exit is judged against (unit
3.5) and it implies later spend, which stays separately gated.
No spend was made or committed in this unit; research was
reading public pages only. One source (the Tiny Tapeout price
calculator) renders only inside the submission app and one
(ChipFoundry's portal-side precheck detail) sits behind an
account; both are named as gaps where they occur, per the
unit's stop-at-that-source rule.

Pilot: gcd stays the pilot. No route's evidence made a
different pilot strictly better; the fit analysis per route is
in Sections 3.5, 4.6 and 5.4.

## 1. Method

Every route fact below was read live on 2026-10-26 from the
operator's own pages, template repositories, terms, or (for
the Efabless shutdown and the Cadence service launch) the
operator's or foundry's own announcements. The full source
list is Section 9. Facts that could not be verified from a
primary page are flagged in place and collected in
Section 9.2; nothing in the comparison rests on them.

## 2. The landscape changed since this phase was scoped

The queue scoped unit 3.3 against "TinyTapeout shuttle,
Efabless ChipIgnite, and any other route". That framing is
one operator out of date:

- Efabless Corporation shut down at the end of February 2025
  after failing to close a funding round (its own notice, as
  reported 2025-03-03: "Efabless has shut down operations
  until further notice"). chipIgnite as an Efabless service
  stopped with it, the then-current Tiny Tapeout Sky130
  shuttle (TT10) was cancelled, and TT08/TT09 were left in
  fabrication limbo at the time.
- On 2025-09-03, Umbralogic Technologies LLC, operating as
  ChipFoundry, announced it had acquired Efabless's
  intellectual property and technology assets. Read today,
  efabless.com carries only that acquisition notice and
  points at ChipFoundry.
- ChipFoundry now operates chipIgnite directly (Section 4)
  and is also the manufacturing back end for Tiny Tapeout's
  Sky130 shuttles: the current TT Sky130 runs ride ChipFoundry
  CI shuttles (TTSKY26d rides CI-2612, the same shuttle a
  direct chipIgnite project would book). Routes A and B are
  therefore two products on one operator and one fab
  (SkyWater), not independent supply chains.
- The free Google / SkyWater Open MPW program submitted
  through the Efabless platform. With the operator gone and
  no 2025 or 2026 Open MPW window announced by Google,
  SkyWater or ChipFoundry, it is recorded as closed, not as a
  route. Aggregator pages still listing it as current are
  stale against the primary notices above.
- One genuinely separate route exists: the Cadence and
  SkyWater MPW aggregation service launched in May 2025 to
  fill the gap Efabless left (Section 5). Same fab, different
  aggregator, no harness.

## 3. Route A: Tiny Tapeout Sky130 (TT SKY)

### 3.1 Status and schedule (read 2026-10-26)

- Open Sky130 run: TTSKY26d, launched 2026-09-22, submission
  closes 2026-11-30, on ChipFoundry shuttle CI-2612. Chips
  expected 2027-05-12, estimated shipping 2027-06-09.
- Next planned Sky130 run: TTSKY27a, submission deadline
  May 2027 (estimate), estimated shipping November 2027.
  TTSKY27b and TTSKY27c follow in September and
  December 2027. Future dates are the operator's estimates
  and its terms state schedules are estimates only.
- Cadence of the route is proven, not promised: TTSKY26a and
  TTSKY26b closed in May 2026 with 289 and 273 designs on
  CI-2605, TTSKY26c closed 2026-09-07 with 242 designs on
  CI-2609.

### 3.2 Harness contract

Tiny Tapeout owns the chip frame: pads, IO cells
(sky130_ef_io_gpiov2_pad), power entry, the project
multiplexer and the demo/breakout boards. The project owns
one tile of user logic behind a fixed module interface:

```verilog
module tt_um_<unique_name> (
    input  wire [7:0] ui_in,    // dedicated inputs
    output wire [7:0] uo_out,   // dedicated outputs
    input  wire [7:0] uio_in,   // bidirectional, input path
    output wire [7:0] uio_out,  // bidirectional, output path
    output wire [7:0] uio_oe,   // bidirectional, enable path
    input  wire       ena,
    input  wire       clk,
    input  wire       rst_n     // active low reset
);
```

- 26 pins total reach the design: clk, rst_n, 8 ui_in,
  8 uo_out, 8 bidirectional uio. Top module name must start
  with `tt_um_` and be unique on the shuttle.
- Tile size: the official pages state about 160 x 100 um for
  a 1x1 tile (FAQ, TT04 to TT10 wording, repeated in the
  teaching pages) and 160 x 225 um for a 1x2 analog slot on
  sky130A, which puts a current 1x1 tile at roughly
  160 x 112 um. Either way a 1x1 tile is roughly 16,000 to
  18,000 um2. Larger configurations (1x2, 2x2, up to 4 tiles
  high) are declared in info.yaml.
- PDK and flow: sky130A, standard cells sky130_fd_sc_hd,
  hardening by LibreLane in the template's GitHub Action
  (the local hardening guide gives LibreLane tag 3.0.3 as
  current and points at the tt-gds-action defaults as the
  authoritative pin). Gap: the exact PDK git revision the
  action pins is not published on the pages read; the action
  defaults are the place it is fixed, and unit 3.4 would
  record the pin it actually ran.
- Analog is supported on the same shuttles through a
  separate template and paid analog pins (up to 6); gcd does
  not need it and this memo does not price it.

### 3.3 The exact submission checklist, item by item

From the SKY Verilog template, the submission guides and the
FAQ, all read this wake. "Evidence" is what the item produces
that unit 3.5 would cite.

1. Project repository created from the Tiny Tapeout SKY
   template (ttsky-verilog-template, Apache-2.0), GitHub
   Actions enabled, GitHub Pages source set to GitHub
   Actions. Evidence: the public repository itself.
2. Design sources in `src/`, top module named `tt_um_*`,
   unique on the shuttle. Evidence: source listing in the
   repository and in the shuttle index.
3. `info.yaml` complete and correct: title, author,
   description, language, clock_hz, tiles, top_module,
   source_files, and a pinout label for the ui, uo and uio
   pins. Evidence: info.yaml at the submitted revision; the
   docs action fails on missing fields.
4. `docs/info.md` filled in: how it works and how to test.
   Leaving template text in place fails the docs action.
   Evidence: the generated project datasheet (PDF) in the
   shuttle documentation.
5. Testbench in `test/` adapted to the design (cocotb);
   the test action passes. Evidence: test action results in
   the repository.
6. GDS action green, which is the hardening gate: LibreLane
   hardens the design, the Tiny Tapeout precheck runs and
   all its checks must be green to submit, a gate-level test
   runs, and a GDS viewer deploys. Evidence: the action run
   (GDS, LEF, utilisation and cell usage, precheck summary,
   GL results) at the submitted revision. Gap: the
   enumerated list of precheck checks is not published on
   the pages read; the rule "all checks green" is. Unit 3.4
   would enumerate the checks from the action it runs.
7. Hardening done for the shuttle's PDK: submission rejects
   a design hardened for a different PDK (the FAQ's example
   is a sky130A design submitted to an IHP shuttle).
   Evidence: the same GDS action record, plus the shuttle
   record naming the SkyWater 130nm open PDK.
8. Tile allocation purchased in the submission app
   (app.tinytapeout.com) for the target run. This is the
   route's spend step and is gated (Section 8). Evidence:
   the app allocation on the project.
9. Project submitted in the app and a revision submitted
   for the current GDS; every design change requires a new
   revision, and no revision is accepted after the closing
   date (2026-11-30 for TTSKY26d). Evidence: the shuttle
   index entry for the project at its final revision.
10. Terms accepted by submitting: the design and its
    documentation are published publicly (website and
    shuttle GitHub repositories, supplied to every other
    participant on the chip), and the design must be
    licensed under Apache-2.0 or compatible terms. This is
    a licence commitment and a publication, both gated on
    Matheus under the standing rules (Section 8).

### 3.4 Cost and what the submitter receives

- Verified price component: 70 EUR per tile (analog specs
  page; the same page's worked examples price digital area
  the same way). That price is for including the design in
  the shuttle and explicitly excludes the ASIC, the PCB and
  shipping.
- The physical chip is a separate purchase: a devkit is one
  demo board plus one breakout board carrying the shuttle
  ASIC, and the FAQ is explicit that a submitter receives
  1 chip per devkit and cannot buy the bare chip alone.
- Gap: the current devkit price and the all-in total for a
  1-tile TTSKY26d submission are not stated on any static
  official page read this wake; the homepage and FAQ both
  defer to the calculator inside the submission app, which
  requires sign-in. Pre-2025 prices ($150 early bird /
  $300 standard for tile plus devkit) are superseded and are
  not used anywhere in this memo. The exact total is a
  number unit 3.4 cannot fix until the app is read at
  purchase time; it is two orders of magnitude below
  Route B on every historical and component data point, but
  the memo records it as unverified rather than guessing.
- Chips expected 2027-05-12 for TTSKY26d, shipping estimate
  2027-06-09. The FAQ warns total waits can run to a year.

### 3.5 gcd fit

- Area: fits one tile with wide margin. gcd's Phase 0
  sky130hd finish instance area is 3,200.57 um2 and its
  whole die, padframe-less core plus margins, is
  6,887.34 um2, against roughly 16,000 to 18,000 um2 for a
  1x1 tile. No multi-tile purchase is needed.
- Interface: does not fit directly, and this is the route's
  real unit 3.4 work. gcd presents 55 signal pins (clk,
  reset, req_val, req_rdy, req_msg[31:0], resp_val,
  resp_rdy, resp_msg[15:0], from the unit 3.2 SPEF port
  list). The tile offers 24 general IO plus clk and rst_n.
  A wrapper must serialize the 32-bit request in and the
  16-bit response out across the 8-bit ports and translate
  the valid/ready handshake. That wrapper is new design
  work owned by unit 3.4, not a harness change.
- Clock: the ORFS baseline constrains gcd at 3.4 ns; the
  tile clock is board-supplied and the FAQ rates the design
  clock at 50 MHz or more. The wrapper would run gcd at the
  board clock; functional behaviour is clock-rate agnostic,
  timing closure at the lower rate is strictly easier.

## 4. Route B: chipIgnite direct (ChipFoundry)

### 4.1 Status and schedule (read 2026-10-26)

chipIgnite is the full-chip product: the submitter books a
whole Caravel-class chip on a ChipFoundry MPW shuttle, not a
tile on a shared one.

- CI2612 (the current bookable run): commitment date
  2026-11-04, tapeout date 2026-12-07, delivery date
  2027-05-25. 100 QFN samples, evaluation board included,
  bare die option available.
- CI2705: commitment 2027-03-20, tapeout 2027-05-19,
  delivery 2027-11-03. CI2709: commitment 2027-07-24,
  tapeout 2027-09-22, delivery 2028-03-08.
- A shuttle proceeds only with at least 20 confirmed
  participants; ChipFoundry may reschedule or cancel below
  the threshold, refunding the reservation deposit or
  rolling the project to the next shuttle.
- Note the CI2612 commitment date is 9 days after this memo.
  Choosing Route B for CI2612 is a this-week decision; the
  same product on CI2705 is not.

### 4.2 Cost and payment milestones

- Base price $14,950 USD per project (reservations page and
  commercial terms agree). Included: fabrication, 100 QFN
  packaged parts (10 of them assembled on M.2 daughter
  cards), a standard evaluation board, test framework and
  firmware. Options priced in the terms: 50 additional bare
  die for $3,000; design review and premium support extra;
  academic and multi-design discounts case by case, rates
  not published.
- $500 non-refundable reservation deposit per project.
- 50 percent of the project cost due by the commitment
  deadline, stated as 60 days before the final design
  submission deadline; the remaining balance due 14 days
  before that deadline, with design files updateable until
  the deadline. Delivery is stated as approximately 5 months
  after the submission deadline, consistent with the
  CI2612 dates above.
- Gap: the final design submission deadline date for CI2612
  itself is not published on the pages read; only the
  commitment and tapeout dates are absolute.

### 4.3 Harness contract

ChipFoundry owns the frame and the management SoC; the
project owns the user project area inside it.

- Caravel harness: RISC-V management SoC with peripherals,
  housekeeping SPI, clocking and POR; user project area
  fixed at 2.92 mm x 3.52 mm (about 10.28 mm2) with 38 GPIO
  pads and 4 power pads. The user wrapper
  (`user_project_wrapper`, fixed pin order and fixed area,
  both from the template and not changeable) exposes a
  32-bit Wishbone port to the management SoC, 128 logic
  analyzer probes, the 38 GPIOs, analog IO on the Caravan
  variant, 3 user interrupts and a secondary clock.
- GPIO 0 to 4 are fixed system pins; GPIO 5 to 37 are
  user-configurable and the configuration is a submission
  input (Section 4.4).
- Variants: Caravel (management SoC, 38 GPIO) is the
  chipIgnite default in the offering text (RISC-V subsystem
  is part of the $14,950 bundle). An OpenFrame variant (no
  management SoC, padframe only) exists in the same template
  family; the offering's "up to 15 mm2" figure belongs to
  that family, not to the Caravel user area, and a Double
  Wide OpenFrame is announced without published dimensions.
  A Route B decision would fix the variant; this memo
  evaluates Caravel, the documented default.
- PDK and flow: sky130A through the template's pinned
  collateral (Caravel Lite tag and open_pdks commit are
  Makefile pins; the sky130A open_pdks commit was visible
  only truncated, prefix 3e0e31dc, in the repo index read
  this wake, so the full pin is a gap unit 3.4 would read
  from the Makefile itself). Hardening is LibreLane (the
  template's fork lineage from OpenLane 2) driven by the
  `cf` CLI.

### 4.4 The exact submission checklist, item by item

The template repository's "Checklist for Shuttle
Submission" is six items, quoted in substance:

1. Top-level macro is named `user_project_wrapper`.
2. Full chip simulation passes for both RTL and GL.
3. Hardened macros are LVS and DRC clean.
4. `user_project_wrapper` matches the required pin order
   and template.
5. Design passes the local `cf precheck`.
6. Documentation (the README) is updated with
   project-specific details.

Around those six, the flow as documented in the template
and the `cf` CLI documentation demands, each with its
evidence:

7. GPIO configuration completed (`cf gpio-config`) before
   verification or precheck; recorded in `user_defines.v`.
   Evidence: the committed user_defines.v and project
   configuration.
8. Multi-corner STA run (`caravel-sta`: typical, fast and
   slow corners) on the hardened wrapper with extracted
   parasitics. Evidence: STA reports in the project.
9. `cf precheck` itself runs the MPW precheck set: licence,
   Makefile structure, documentation, consistency, GPIO
   defines, GDS XOR, Magic DRC, KLayout DRC checks, LVS and
   antenna/density checks in the OpenFrame variant's list.
   Evidence: the precheck report. LVS may be skipped in
   development (`--disable-lvs`) but must pass for
   submission.
10. Submission mechanics: `cf push` uploads the project
    (exactly one wrapper GDS in `gds/`, `user_defines.v`,
    metadata; the platform records the GDS hash), results
    and the consolidated tapeout report come back through
    the platform, and `cf confirm` marks the submission
    Final, a step the CLI warns cannot be easily undone.
    Evidence: the platform project state and the
    consolidated tapeout report.
11. Payment milestones of Section 4.2 met on their dates;
    they are part of the checklist in effect, since a missed
    milestone cancels the project per the commercial terms.

### 4.5 Confidentiality and IP

The commercial terms state the customer retains all IP in
their design, ChipFoundry keeps designs confidential, and an
NDA is available on request. This is the opposite posture
from Route A's public-by-construction submission, and it is
Route B's strongest differentiator.

### 4.6 gcd fit

Trivial on area (gcd's whole Phase 0 die is about 0.07 of
the Caravel user area) and easy on interface: the 55 gcd
pins map onto the 38 GPIOs plus the logic analyzer and
Wishbone paths with room to spare, and a Wishbone-native
gcd wrapper is a smaller change than Route A's serializer.
No fit obstacle. The obstacles are price, the CI2612
commitment date, and checklist weight (full-chip simulation
with the management SoC in the loop, GPIO configuration,
multi-corner STA through the template's scripts).

## 5. Route C: Cadence and SkyWater MPW aggregation

### 5.1 Status

Launched 2025-05-05 by Cadence and SkyWater explicitly
because the previous SKY130 aggregator (Efabless) had gone
out of business. The last official window found this wake:
submissions accepted through Spring 2026, with expected die
delivery July 2026. Gap, and it is decisive for planning: no
official page found this wake announces a Fall 2026 or 2027
window. Whether a slot can be booked today is unverifiable
from public pages; the route starts with an email to
SKY130MPW@cadence.com, and its availability would have to be
confirmed there before any plan could rest on it.

### 5.2 Cost and what the submitter receives

- $10,000 per design (SkyWater's announcement) to $10,000
  to $12,000 plus tax and shipping (Cadence's shuttle post;
  two process options, with and without ReRAM, explain the
  range; which option maps to which price is not stated).
  Payment is by purchase order after a legal agreement.
- The submitter receives 40 bare die. Packaging is not
  included (Cadence refers participants to outside packaging
  suppliers), and bring-up and testing are the submitter's.

### 5.3 Harness contract: there is none

This route breaks the Phase 3 framing assumption. Cadence
supplies a seal ring (with an integrated die ID) to place at
the origin of the top level; everything else in the frame is
the submitter's: pads, ESD, power entry and the padframe are
project-owned design work. The layout must fit a fixed
bounding box of 3.588 mm x 5.188 mm. Verified checklist
items from the official pages:

1. Contact Cadence and sign the legal agreement; submit a
   purchase order.
2. Layout inside the bounding box, seal ring with die ID at
   0,0 in the top level.
3. Upload an OASIS file to the Cadence-provided FTP site
   (any EDA tools allowed; no IP restriction stated; no NDA
   needed for the SKY130 PDK).
4. Design DRC-clean under the Cadence Pegasus decks built on
   the SKY130 rules, checking manufacturing and recommended
   rules, plus Cadence's data integrity checks. Evidence is
   Cadence's acceptance feedback, stated at about 2 weeks
   after submission in the Spring 2026 window.
5. Functionality and performance are the design owner's
   responsibility entirely; there is no harness simulation
   environment to pass.

### 5.4 gcd fit

Area fits easily (the bounding box is about 18.6 mm2
against gcd's 0.0069 mm2 die). But gcd exists only as a
core: it has no padframe, no ESD, no IO cells. Route C turns
unit 3.4 from "wrap gcd to a harness contract" into "design
a padframe and chip frame", which the Phase 3 breakdown
explicitly scoped out, and it returns bare die that then
need a separately sourced packaging step before any
board-level test. Combined with the unverified availability,
Route C is not a fit for this phase even at its lower than
Route B price.

## 6. Checked and excluded

- Google / SkyWater Open MPW: closed with its operator
  (Section 2). Not a route in 2026.
- wafer.space: a real, currently selling MPW service, but
  every slot on its official page is GF180MCU
  (GlobalFoundries 180 nm), not Sky130. Excluded on process.
- Tiny Tapeout IHP shuttles: IHP sg13g2, a different 130 nm
  PDK, not Sky130. Excluded on process. (Its chip-loan terms
  would also have changed the deliverable.)
- Tiny Tapeout GF shuttles: GF180MCU via wafer.space.
  Excluded on process.
- Zero to ASIC course: a paid course whose silicon ticket is
  fulfilled through Tiny Tapeout. An access channel to
  Route A, not a separate route.
- FOSSi Foundation / Dial-Up: a foundation and a talk
  series; no shuttle, price or submission window exists
  there. Not an operator.
- MOSIS, Europractice, CMC: no Sky130 offer found on their
  material this wake. Not listed as routes on that evidence.
- SkyWater direct: SkyWater states it runs MPW programs,
  and its public Sky130 path is the Cadence service of
  Section 5. No public self-serve Sky130 shuttle with price
  and checklist was found. Not listed as a separate route.
- OpenTapeout: no official site, price, schedule or harness
  found. No evidence it exists as a route.

## 7. Comparison

| Axis | A: TT SKY | B: chipIgnite | C: Cadence MPW |
|---|---|---|---|
| Operator | Tiny Tapeout on ChipFoundry CI shuttles | ChipFoundry | Cadence for SkyWater |
| Bookable today | Yes, TTSKY26d closes 2026-11-30 | Yes, CI2612 commitment 2026-11-04 | Unverified, last window Spring 2026 |
| Cost to silicon | 70 EUR per tile plus devkit (total unverified, app only) | $14,950 all-in per project | $10,000 to $12,000 plus tax/shipping, packaging extra |
| What arrives | 1 chip on a devkit (demo + breakout boards) | 100 QFN parts, eval board, test firmware | 40 bare die, nothing else |
| Time to parts | Chips expected 2027-05-12 (26d) | Delivery 2027-05-25 (CI2612) | July 2026 for the Spring window; next window unknown |
| Harness | TT frame and mux own pads/ESD/power; project is a tile | Caravel SoC owns pads/ESD/power; project is the user area | None; project owns pads/ESD/power, Cadence supplies seal ring only |
| gcd interface work | Serializer wrapper over 24 IO (55 pins do not fit) | Wishbone/GPIO wrapper, parallel access fits | Full padframe design, out of Phase 3 scope |
| Checklist weight | Repo, info.yaml, docs, tests, GDS action with precheck all green | Six-item checklist plus GPIO config, full-chip RTL+GL sim, 3-corner STA, cf precheck, platform confirm | Agreement, PO, OASIS, Pegasus DRC and data integrity acceptance |
| Openness | Public by construction; Apache-2.0 compatible licence required | Confidential; customer keeps IP, NDA available | No IP restriction stated |
| Evidence EOS already holds | Units 3.1/3.2 PV and correlation are on the same sky130 PDK family and fd_sc_hd library the tile hardens against | Same PDK family, but the template's own collateral pin differs from the EOS package pin and is only partly verified | Pegasus DRC deck is a different signoff deck from the KLayout deck unit 3.1 ran; PV evidence transfers least well |
| Main risk | Schedule is an estimate; licence/publication gate; total price read only in the app | Price; CI2612 commitment lands 2026-11-04; 20-participant minimum | Availability unverified; padframe scope break; bare die with no package path included |

## 8. Recommendation and the gated remainder

Recommendation: Route A, Tiny Tapeout Sky130, with gcd as
the pilot. It is the only route whose full checklist is
public, runnable end to end without payment up to the
purchase step, and already matched to the evidence units 3.1
and 3.2 produced (same PDK family, same fd_sc_hd library).
Its cost is the lowest by a wide margin, its cadence is
proven across three closed 2026 runs, and its one real
design cost, the serializer wrapper, is exactly the kind of
bounded unit 3.4 work the phase planned for. Route B stays
the fallback if a private, full-chip, 100-part pilot is ever
wanted; Route C is not recommended for this phase
(unverified availability, plus a padframe design task the
phase scoped out).

Target run within Route A: TTSKY26d (closes 2026-11-30) is
reachable only if the two gated items below are decided
promptly, because the serializer wrapper and a green GDS
action have to exist before a revision can be submitted.
If they are not, the same checklist on TTSKY27a (deadline
May 2027, estimate) is the target, and unit 3.5 is judged
against the identical checklist either way.

The gated remainder, stated verbatim for the decision:

- Spend, Route A: purchase of a tile allocation and a
  devkit for the chosen TT SKY run in the Tiny Tapeout
  submission app, at the total the app's calculator shows at
  purchase time (per-tile component verified at 70 EUR; the
  all-in total was not published on any static page read for
  this memo).
- Licence and publication, Route A: the pilot project (the
  gcd wrapper project, not EOS itself) is published publicly
  and licensed under Apache-2.0 or compatible terms, as the
  Tiny Tapeout terms require of every submitted design.
- Spend, only if Route B were chosen instead: a $500
  non-refundable reservation deposit, then 50 percent of
  $14,950 by the commitment deadline and the balance 14 days
  before the submission deadline for the chosen CI shuttle.

## 9. Sources and gaps

### 9.1 Sources read live on 2026-10-26

Tiny Tapeout: https://tinytapeout.com/ ,
https://tinytapeout.com/chips/ ,
https://tinytapeout.com/chips/ttsky26a/ ,
https://tinytapeout.com/faq/ ,
https://tinytapeout.com/specs/analog/ ,
https://tinytapeout.com/specs/gpio/ ,
https://tinytapeout.com/hdl/important/ ,
https://tinytapeout.com/guides/local-hardening/ ,
https://tinytapeout.com/guides/advanced-workshop/submit-your-design/ ,
https://tinytapeout.com/terms/ ,
https://tinytapeout.com/news/sky130-confirmed/ ,
https://tinytapeout.com/news/efabless-shutsdown/ ,
https://github.com/TinyTapeout/ttsky-verilog-template ,
https://app.tinytapeout.com/ (run list and countdown only;
pricing behind sign-in, not read).

ChipFoundry: http://chipfoundry.io/ ,
https://chipfoundry.io/chipignite ,
https://chipfoundry.io/reservations ,
https://chipfoundry.io/commercial-terms ,
https://github.com/chipfoundry/caravel_user_project ,
https://pypi.org/project/chipfoundry-cli/ (CLI versions
1.2.0 and 2.2.1 pages),
https://efabless.com/ (acquisition notice only).

Shutdown and acquisition reporting:
https://www.hackster.io/news/open-source-silicon-project-tiny-tapeout-hits-trouble-as-efabless-shuts-its-doors-9ac7fab1649d ,
https://www.eenewseurope.com/en/tiny-tapeout-hit-as-efabless-closes/ ,
and the 2025-09-03 Globe Newswire acquisition release as
republished at
https://markets.financialcontent.com/fatpitch.valueinvestingnews/article/gnwcq-2025-9-3-chipfoundry-acquires-efabless-assets-to-propel-custom-silicon-innovation .

Cadence and SkyWater:
https://www.skywatertechnology.com/lowering-barriers-to-silicon-cadence-launches-mpw-aggregation-service-for-sky130/ ,
https://community.cadence.com/cadence_blogs_8/b/corporate-news/posts/next-steps-for-the-cadence-and-skywater-mpw-service .

Excluded-route checks: https://wafer.space/ ,
https://Zerotoasiccourse.com/digital/ ,
https://www-archive.fossi-foundation.org/dial-up/ ,
http://opensource.googleblog.com/2022/10/announcing-globalfoundries-open-mpw-shuttle-program.html .

EOS-side facts are from this repository: gcd areas from
docs/phase0-exit-report.md, gcd port list from the unit 3.2
SPEF evidence in docs/extraction-correlation.md's inputs.

### 9.2 Unverified items (nothing above rests on these)

1. Tiny Tapeout all-in price for a 1-tile TTSKY26d
   submission with devkit and shipping (app calculator
   only, behind sign-in).
2. The enumerated Tiny Tapeout precheck check list for the
   current GDS action (the all-green rule is published; the
   list is not, on the pages read).
3. The exact PDK git revision pinned by the current
   tt-gds-action (sky130A and LibreLane 3.0.3 are verified;
   the PDK hash is not published on the pages read).
4. ChipFoundry CI2612's final design submission deadline
   date (commitment 2026-11-04 and tapeout 2026-12-07 are
   published; the submission deadline is expressed only
   relative to the payment milestones).
5. The full ChipFoundry-era open_pdks commit for sky130A
   (truncated prefix 3e0e31dc visible in a repo index; the
   Makefile in the template repo is the authoritative read).
6. Any Cadence MPW window after Spring 2026, and which of
   its two process options carries the $10,000 versus the
   $12,000 price.
7. Double Wide OpenFrame dimensions (announced, not
   published on the pages read).
