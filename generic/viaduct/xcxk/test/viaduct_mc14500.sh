#!/usr/bin/env bash

yosys -p "tcl ../projectXCxk/synth/scripts/synth.tcl ../projectXCxk/synth/examples/mc14500.v mc14500 ./"

# Run PnR
~/nextpnr/nextpnr-generic --uarch xcxk --json mc14500_synth.json -o device=3195A $1 --write pnr_mc14500.json
