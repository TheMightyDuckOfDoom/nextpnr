#!/usr/bin/env bash

yosys -p "tcl ../projectXCxk/synth/scripts/synth.tcl ../../../examples/blinky.v top ./"

# Run PnR
~/nextpnr/build/nextpnr-generic --uarch xcxk --json top_synth.json -o device=3020 $1 --write pnr_blinky.json
