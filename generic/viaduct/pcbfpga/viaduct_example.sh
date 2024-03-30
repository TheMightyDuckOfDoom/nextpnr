#!/usr/bin/env bash
set -ex
# Run synthesis
yosys -p "tcl synth_pcbfpga.tcl blinky.json" ../../examples/blinky.v
# Run PnR
config=$(realpath ~/pcbfpga/config.json)
nextpnr-generic --uarch pcbfpga --json blinky.json --router router2  --write pnrblinky.json --vopt "config=$config"
