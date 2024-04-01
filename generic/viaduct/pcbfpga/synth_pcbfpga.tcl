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
stat
techmap -map +/techmap.v
opt -fast
dfflegalize -cell \$_DFF_P_ x -cell \$_DFFE_PP_ x -cell \$_SDFF_PP0_ x -cell \$_SDFF_PN0_ x -cell \$_SDFFCE_PN0P_ x -cell \$_SDFFCE_PP0P_ x -minsrst 4 -mince 4
stat
abc -lut $LUT_K -dress
clean
techmap -D LUT_K=$LUT_K -map [file dirname [file normalize $argv0]]/pcbfpga_map.v
clean
hierarchy -check
stat

if {$argc > 0} { 
  yosys write_json [lindex $argv 0].json
  yosys write_verilog [lindex $argv 0]_synth.v
}
