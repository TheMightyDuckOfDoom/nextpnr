#!/usr/bin/env bash
set -ex
yosys -p "tcl synth_pcbfpga.tcl ~/servisia/out/servisia.v servisia"
./../../../../nextpnr-generic --uarch pcbfpga --json servisia_synth.json --write pnrservisia.json --vopt=clbs=11x12 --timing-allow-fail
#yosys -p "read_verilog -lib prims.v; read_json pnrservisia.json; dump -o servisia.il; show -format png -prefix servisia"
