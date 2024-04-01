# Usage
# tcl synth_pcbfpga.tcl {out.json}
yosys -import

set LUT_K [lindex $argv 1]

read_verilog -lib [file dirname [file normalize $argv0]]/pcbfpga_prims.v
hierarchy -check -top [lindex $argv 2]
yosys proc
flatten
tribuf -logic
deminout
synth -run coarse
memory_map
opt -full
iopadmap -bits -inpad INBUF O:PAD -outpad OUTBUF I:PAD
techmap -map +/techmap.v
opt -fast
dfflegalize -cell \$_DFF_P_ 0
abc -lut $LUT_K -dress
clean
techmap -D LUT_K=$LUT_K -map [file dirname [file normalize $argv0]]/pcbfpga_map.v
clean
hierarchy -check
stat

if {$argc > 0} { yosys write_json [lindex $argv 0] }
