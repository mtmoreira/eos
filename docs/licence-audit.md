# Licence audit and version pins, Phase 0

Date: 2026-10-18. Scope: the licences of the components EOS plans to
adopt, and the version pin chosen for each. Method: the GitHub API
licence detection for each upstream repository, with the actual licence
files read by hand wherever the detector reported no standard licence
(ABC, Magic, Netgen, ORFS). Pins are the newest stable release, tag, or
head commit visible on that date.

This audit covers copyright licences of adopted code only. It is
engineering diligence, not legal advice. The separate patent
freedom-to-operate question from the plan's risk list is not addressed
here and remains open.

## Pins

| Component | Pin | Licence | Class |
|---|---|---|---|
| slang | v12.0 | MIT | permissive |
| Yosys | v0.70 | ISC | permissive |
| ABC | commit 416199292fbd | UC Regents custom (copyright.txt) | permissive |
| mockturtle | v0.3 | MIT | permissive |
| OpenSTA | commit 0f2529d18218 | GPL-3.0 (dual-licensed, Parallax) | copyleft |
| OpenDB (via OpenROAD) | v2.0, commit a5a56584fed1 | BSD-3-Clause | permissive |
| OpenROAD modules | v2.0, commit a5a56584fed1 | BSD-3-Clause | permissive |
| KLayout | v0.30.12 | GPL-3.0 | copyleft |
| Magic | 8.3.685 | UC Regents custom (LICENSE) | permissive |
| Netgen | commit e5a185dcfa16 | GPL-1.0 (per the repo Copying file) | copyleft |
| ORFS (baseline only) | tag v3.0, commit 181e91337761 | BSD-3-Clause (build and run scripts) | permissive |
| open_pdks | commit abfc54bd87e0 | Apache-2.0 | permissive |

The machine-readable copy of this table is third_party_manifest.yaml;
where the two disagree, the manifest governs and this document should
be corrected.

## Findings

1. The copyleft set is exactly three components: OpenSTA (GPL-3.0),
   KLayout (GPL-3.0) and Netgen (GPL-1.0 per its Copying file). This
   matches the licence posture decided on 2026-10-18: GPL components
   sit behind clean module boundaries (OpenSTA in
   src/analysis/timing/sta, KLayout behind src/io/gds and the pv
   modules, Netgen behind src/pv/lvs), so the distribution decision
   stays open.

2. OpenSTA has no maintained release line: its only tag, v2.2.0, dates
   from 2020, while development continues on the default branch. EOS
   therefore pins commits, and at import time should record which
   OpenSTA commit the pinned OpenROAD vendors, as the correlation
   reference for timing results.

3. ABC and Netgen publish no usable release tags, so both are pinned
   by commit. A pin change for either is a normal reviewed change
   under the manifest rule.

4. mockturtle's newest release, v0.3, dates from 2022. That is
   acceptable for a header-only library, but the pin should be
   re-evaluated against the default branch at import time.

5. ABC and Magic carry UC Regents custom licence texts. Both are
   permissive in substance (use, copy, modify and distribute, with the
   copyright notice retained), but they are not standard SPDX texts.
   The exact licence file must travel with the imported code, and the
   attribution requirement applies to both.

6. Netgen's Copying file is the GNU GPL version 1 text (1989), not
   version 2 or 3. The lvs module boundary must treat Netgen as
   GPL-1.0, and per-file headers must be rechecked at import time,
   because individual files may state different terms.

7. ORFS is a baseline reference only and is never imported. Its build
   and run scripts are BSD-3-Clause, but the designs and platforms it
   runs carry their own licences. Baseline work may publish measured
   numbers; it must not redistribute ORFS design files inside EOS.

8. open_pdks moved from RTimothyEdwards to the fossi-foundation
   organisation. EOS pins commit abfc54bd87e0 and hashes every payload
   it produces, per platforms/sky130/manifest.yaml.

## Rule going forward

Pins change in their own commits, with a reason, never silently as
part of a feature change. The same discipline as QoR baselines: a pin
bump is a reviewed event, because every result EOS reports is only
meaningful against known versions of the engines underneath it.
