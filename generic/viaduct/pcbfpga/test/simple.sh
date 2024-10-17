#!/usr/bin/env bash
set -ex
yosys -p "tcl synth_pcbfpga.tcl ./blinky.v blinky"
./../../../../nextpnr-generic --uarch pcbfpga --json blinky_synth.json --write pnrblinky.json
#yosys -p "read_verilog -lib prims.v; read_json pnrblinky.json; dump -o blinky.il; show -format png -prefix blinky"
