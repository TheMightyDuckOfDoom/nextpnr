#!/usr/bin/env bash
set -ex
yosys -p "tcl ./scripts/synth_pcbfpga.tcl ./examples/blinky.v blinky"
./../../../../nextpnr-generic --uarch pcbfpga --json ./out/blinky_synth.json --write ./out/pnrblinky.json --vopt=clbs=2x2 --timing-allow-fail
#yosys -p "read_verilog -lib prims.v; read_json pnrblinky.json; dump -o blinky.il; show -format png -prefix blinky"
yosys -p "read_verilog -lib ./scripts/prims.v; read_json ./out/pnrblinky.json; write_verilog ./out/pnrblinky.v"
