# package (L2 design-data)

Models a technology package as data and checks its internal consistency.

A TechnologyPackage names every collateral item the flow consumes (Liberty,
tech and cell LEF, GDS, CDL and SPICE views, verification runsets, RC rules),
the cells as seen in the Liberty, LEF and logical views, macros and their
views, special cells, scenarios, supply aliases and units. CheckPackage in
include/eos/tech/package.h applies the bring-up rules to that data and
returns one Finding per problem, sorted by rule and subject: version and
hash completeness, duplicate collateral ids, cell presence and pin agreement
across views, PG pin agreement, unambiguous supply aliases, present units,
macro required views, black box abstraction contracts, known special cells
and scenarios backed by Liberty corners. An empty result means the package
is consistent.
