#!/usr/bin/env bash
set -ex
# Run synthesis
yosys -p "tcl synth_pcbfpga.tcl mini_example.json 4" mini_example.v
# Run PnR
config=$(realpath ~/pcbfpga/config.json)
nextpnr-generic --uarch pcbfpga --json mini_example.json --router router2  --write pnrmini_example.json --vopt "config=$config"
