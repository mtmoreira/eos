# P7 correlation note (fixture numbers only)

## Fixture evidence (measured, unit P7)

The finish fixture (two BUFX2 instances, three signal nets, two
POWER/GROUND special nets, three CORE SPACER filler masters) runs
floorplan, place, global plus detailed route, then finish and
write_gds. Measured in the P7 gate run (ctest pd.finish 20/20,
ui.registry 101/101):

- `eos::finish` (no options): fillers=18, grid_shapes=0,
  metal_fills=0, pdn=0.
- `eos::finish -pdn`: fillers=18, grid_shapes=21, metal_fills=0,
  pdn=1. Both supply nets carry special wires after the grid
  (VDD and VSS each report special-wire count greater than 0).
- `eos::finish -fill_config <fixture rules>`: fillers=18,
  metal_fills=79.
- Repeat finish is idempotent (second run adds no fillers, no
  grid shapes, no fills).
- `eos::write_gds` after finish: gds_size=3714 bytes,
  instances=20 (2 design instances plus 18 fillers),
  structures=5 (top plus BUFX2, FILL1, FILL2, FILL4).
  The GDS decodes exactly for the fixture shape (linear nets);
  branched-wire junction seeding is best-effort, as the module
  README states.

These are fixture numbers, not process values: the LEF, DEF,
liberty, and metal-fill rules in the test are invented for the
fixture, and no number here is presented as a process-accurate
value.

## ORFS gcd baseline (reference only, not correlated)

The plan's ORFS gcd baseline for the finish stage is: area
2623.77 um^2, finish worst slack +0.100989 ns, power 1.93 mW,
433 instances. No delta against this baseline is claimed in
this unit, because the gcd evidence run was infeasible (below).
A delta computed from the fixture would be meaningless, so
none is stated.

## GCD evidence run: infeasible, verbatim evidence

The workspace gcd database candidate
(`openroad-src/test/gcd_sky130hd.odb`) is rejected by the EOS
database reader. Run through the built `eos` shell, 2026-11-03:

```
eos::import_odb /home/hatch/workspace/openroad-src/test/gcd_sky130hd.odb
EOS io_error: could not read database file: /home/hatch/workspace/openroad-src/test/gcd_sky130hd.odb
```

This is the same rejection P3, P4, and P5 recorded for this
file, and no LEF/DEF import command exists in EOS yet, so
there is no second path from ORFS gcd sources into the EOS
database. The gcd finish, GDSII, DRC, and LVS correlation
therefore remains owed to a later unit that lands a LEF/DEF
import command or a compatible database reader. It is not
waved through here.

## DRC and LVS verdicts

No DRC or LVS verdict on gcd is stated in this unit: no gcd
GDS exists (see above), and PV stays orchestration in this
phase (KLayout DRC, Netgen LVS as external evidence tools
only, no in-tree DRC/LVS/antenna). The fixture GDS is
written and its structure counts are measured above; it has
not been run through KLayout or Netgen, and no clean verdict
is claimed for it.

## Carried items

- The P2 PG verdict is still owed at P7 exit (carried from P2).
- Real-Liberty scratch-STA, gcd correlation, and gcd DRC/LVS
  verdicts remain owed as stated above.
