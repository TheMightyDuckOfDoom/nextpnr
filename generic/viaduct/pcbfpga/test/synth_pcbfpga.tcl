# Usage
# tcl synth_pcbfpga.tcl {out.json}
yosys -import

set LUT_K 4

yosys read_verilog -lib [file dirname [file normalize $argv0]]/prims.v
yosys read_verilog [lindex $argv 0]
hierarchy -check -top [lindex $argv 1]
yosys proc
flatten
tribuf -logic
deminout
synth -run coarse
memory_map
opt -full
iopadmap -bits -inpad IBUF O:PAD -outpad OBUF I:PAD
stat
techmap -map +/techmap.v
opt -fast
dfflegalize -cell \$_DFF_P_ 0
stat
abc -lut $LUT_K -dress
clean
techmap -D LUT_K=$LUT_K -map [file dirname [file normalize $argv0]]/cells_map.v
clean
stat
#extract -map [file dirname [file normalize $argv0]]/combine_lut3.v
#stat
hierarchy -check
stat

yosys write_json [lindex $argv 1]_synth.json
yosys write_verilog [lindex $argv 1]_synth.v
