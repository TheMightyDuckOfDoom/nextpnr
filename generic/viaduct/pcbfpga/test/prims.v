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

module DFF #(
	parameter ENABLE_USED = 1'b0,
	parameter RST_USED = 1'b0
) (
	input CLK, D, EN, RST_N,
	output reg Q
);
	initial Q = 1'b0;
	always @(posedge CLK) begin
		if (RST_USED && !RST_N)
			Q <= 1'b0;
		else if (!ENABLE_USED || (ENABLE_USED && EN))
			Q <= D;
	end
endmodule

module IOB #(
	parameter INPUT_USED = 1'b0,
	parameter OUTPUT_USED = 1'b0,
	parameter ENABLE_USED = 1'b0
) (
	(* iopad_external_pin *) inout PAD,
	input I, EN,
	output O
);
	generate if (OUTPUT_USED && ENABLE_USED)
		assign PAD = EN ? I : 1'bz;
	else if (OUTPUT_USED)
		assign PAD = I;
	endgenerate

	generate if (INPUT_USED)
		assign O = PAD;
	endgenerate
endmodule

module IBUF (
	(* iopad_external_pin *) input PAD,
	output O
);
	IOB #(
		.INPUT_USED(1'b1)
	) _TECHMAP_REPLACE_ (
		.PAD(PAD),
		.I(),
		.EN(1'b1),
		.O(O)
	);
endmodule

module OBUF (
	(* iopad_external_pin *) output PAD,
	input I
);
	IOB #(
		.OUTPUT_USED(1'b1)
	) _TECHMAP_REPLACE_ (
		.PAD(PAD),
		.I(I),
		.EN(),
		.O()
	);
endmodule
