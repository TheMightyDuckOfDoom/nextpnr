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

module INBUF (
    input  PAD,
    output O,
);
    assign O = PAD;
endmodule

module OUTBUF (
    output PAD,
    input  I,
);
    assign PAD = I;
endmodule

