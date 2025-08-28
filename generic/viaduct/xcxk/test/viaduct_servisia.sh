#!/usr/bin/env bash

yosys -p "tcl ../projectXCxk/synth/scripts/synth.tcl ~/servisia/out/servisia.v servisia ./"

# Run PnR
~/nextpnr/nextpnr-generic --uarch xcxk --json servisia_synth.json -o device=3195A $1 --write pnr_servisia.json
