module PCBFPGA_LUT #(
		parameter            K    = 4,
		parameter [2**K-1:0] INIT = 0
) (
		input [K-1:0] I,
		output        F
);
		assign F = INIT[I];
endmodule

module PCBFPGA_FF #(
		parameter NO_ENABLE        = 0,
		parameter HAS_RESET        = 0,
		parameter ACTIVE_LOW_RESET = 0
)(
		input      CLK,
		input      RST,
		input      EN,
		input      D,
		output reg Q
);
		always @(posedge CLK) begin
				if(EN | NO_ENABLE) begin
					 	if(HAS_RESET & (RST ^ ACTIVE_LOW_RESET))
								Q <= 1'b0;
						else
								Q <= D;
				end
		end
endmodule

module ibuf (
    (* iopad_external_pin *) input i,
    output o
);
endmodule

module obuf (
    input i,
    (* iopad_external_pin *) output o
);
endmodule

module IOB #(
	parameter OUTPUT = 0,
	parameter ENABLE_OUTPUT = 0
)(
    (* iopad_external_pin *) inout  PAD,
    input  I,
		input  EN,
		output O
);
	assign O = PAD;
	assign PAD = (OUTPUT & (EN | ENABLE_OUTPUT)) ? I : 1'bZ;
endmodule

