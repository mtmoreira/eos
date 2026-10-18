# EOS

EOS, the EDA Open System, is an integrated implementation tool that takes a
design from RTL to GDSII in a target technology: synthesis with STA-driven
optimization, floorplanning, placement, clock tree synthesis, routing, chip
finish, physical verification (DRC, LVS, antenna), signoff STA on extracted
parasitics, and an ECO flow. The first target technology is Sky130.

EOS is one program with one design database and one timing engine, built by
adopting proven open source engines as its own code rather than wrapping
external tools. Optimization priority is lexicographic: timing first, then
power, then area, subject to user constraints.

Status: design phase. The implementation plan and the code structure have
been written; the code structure is approved, the plan is under review, and
no tool code exists yet.

Eos is the Greek goddess of dawn, who opens the gates of heaven each
morning. This is the dawn of open EDA.
