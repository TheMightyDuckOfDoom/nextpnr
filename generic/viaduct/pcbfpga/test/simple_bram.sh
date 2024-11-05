#!/usr/bin/env bash
set -ex
top="ram"
yosys -p "tcl ./scripts/synth_pcbfpga.tcl ./examples/bram.v ${top}"
./../../../../nextpnr-generic --uarch pcbfpga --json ${top}_synth.json --write pnr${top}.json --vopt=clbs=2x2 --timing-allow-fail
#yosys -p "read_verilog -lib prims.v; read_json pnrblinky.json; dump -o blinky.il; show -format png -prefix blinky"
#yosys -p "read_verilog -lib prims.v; read_json pnrblinky.json; write_verilog pnrblinky.v"
