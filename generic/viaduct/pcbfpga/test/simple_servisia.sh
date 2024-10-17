#!/usr/bin/env bash
set -ex
yosys -p "tcl synth_pcbfpga.tcl ~/servisia/out/servisia.v servisia"
./../../../../nextpnr-generic --uarch pcbfpga --json servisia_synth.json --write pnrservisia.json
#yosys -p "read_verilog -lib prims.v; read_json pnrservisia.json; dump -o servisia.il; show -format png -prefix servisia"
