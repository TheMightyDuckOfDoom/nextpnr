#!/usr/bin/env bash
set -ex
# Run synthesis
# opc1:      22 IOB  43 FF  128 LUT
# opc2:      21 IOB  46 FF  125 LUT
# opc3:      35 IOB  59 FF  203 LUT
# opc5:      51 IOB 330 FF  521 LUT
# opc5ls:    55 IOB 354 FF  668 LUT
# opc5ls-xp: 55 IOB 355 FF  905 LUT
# opc6:      57 IOB 351 FF  944 LUT
# opc7:      93 IOB 627 FF 1609 LUT
# opc8:      80 IOB 505 FF 1271 LUT
yosys -p "tcl synth_pcbfpga.tcl opc 4 opc2cpu" ~/opc/opc2/opc2cpu.v
# Run PnR
config=$(realpath ~/pcbfpga/nextpnr_configs/opc.json)
nextpnr-generic --uarch pcbfpga --json opc.json --router router2  --write pnropc.json --vopt "config=$config"
