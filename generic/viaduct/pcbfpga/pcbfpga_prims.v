module LUT #(
	parameter K = 4,
	parameter [2**K-1:0] INIT = 0
) (
	input [K-1:0] I,
	output F
);
	wire [K-1:0] I_pd;

	genvar ii;
	generate
		for (ii = 0; ii < K; ii = ii + 1'b1)
			assign I_pd[ii] = (I[ii] === 1'bz) ? 1'b0 : I[ii];
	endgenerate

	assign F = INIT[I_pd];
endmodule

module DFF (
	input CLK, D,
	output reg Q
);
	initial Q = 1'b0;
	always @(posedge CLK)
		Q <= D;
endmodule

module INBUF (
    input PAD,
    output O,
);
    assign O = PAD;
endmodule

module OUTBUF (
    output PAD,
    input I,
);
    assign PAD = I;
endmodule

