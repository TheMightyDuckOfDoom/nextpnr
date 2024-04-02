#!/usr/bin/env bash
set -ex
# Run synthesis
yosys -p "tcl synth_pcbfpga.tcl servisia 4 servisia" ~/servisia/out/servisia.v
# Run PnR
config=$(realpath ~/pcbfpga/nextpnr_configs/servisia_baseline.json)
nextpnr-generic --uarch pcbfpga --json servisia.json --router router2  --write pnrservisia.json --vopt "config=$config"
