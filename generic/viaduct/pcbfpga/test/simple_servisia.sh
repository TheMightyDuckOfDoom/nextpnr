#!/usr/bin/env bash
set -ex
yosys -p "tcl ./scripts/synth_pcbfpga.tcl ~/servisia/out/servisia.v servisia"
./../../../../nextpnr-generic --uarch pcbfpga --json ./out/servisia_synth.json --write ./out/pnrservisia.json --vopt=clbs=10x10 --timing-allow-fail
#yosys -p "read_verilog -lib prims.v; read_json pnrservisia.json; dump -o servisia.il; show -format png -prefix servisia"
#yosys -p "read_verilog -lib ./scripts/prims.v; read_json ./out/pnrservisia.json; write_verilog ./out/pnrservisia.v"
