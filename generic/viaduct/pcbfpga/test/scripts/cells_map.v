module \$lut (A, Y);
	parameter WIDTH = 0;
	parameter LUT = 0;
	input [WIDTH-1:0] A;
	output Y;

	LUT #(.K(WIDTH), .INIT(LUT)) _TECHMAP_REPLACE_ (.I(A), .F(Y));
endmodule

module \$__SPRAM8K_ (
	input  PORT_RW_CLK,
	input  PORT_RW_WR_EN,
	input  [12:0] PORT_RW_ADDR,
	input  [3:0] PORT_RW_WR_DATA,
	output [3:0] PORT_RW_RD_DATA
);
	BRAM #(
		.DUAL_PORT(1'b0)
	) _TECHMAP_REPLACE_ (
		.CLK(PORT_RW_CLK),
		.WE(PORT_RW_WR_EN),
		.RW_ADDR(PORT_RW_ADDR[8:0]),
		.R_ADDR({5'd0, PORT_RW_ADDR[12:9]}),
		.W_DATA(PORT_RW_WR_DATA),
		.R_DATA(PORT_RW_RD_DATA)
	);
endmodule

module \$__DPRAM512_ (
	input  [8:0] PORT_R_ADDR,
	output [3:0] PORT_R_RD_DATA,

	input  PORT_W_CLK,
	input  PORT_W_WR_EN,
	input  [8:0] PORT_W_ADDR,
	input  [3:0] PORT_W_WR_DATA
);
	BRAM #(
		.DUAL_PORT(1'b1)
	) _TECHMAP_REPLACE_ (
		.CLK(PORT_W_CLK),
		.WE(PORT_W_WR_EN),
		.RW_ADDR(PORT_W_ADDR),
		.R_ADDR(PORT_R_ADDR),
		.W_DATA(PORT_W_WR_DATA),
		.R_DATA(PORT_R_RD_DATA)
	);
endmodule

module \$_DFF_P_ (input D, C, output Q); DFF #(.ENABLE_USED(1'b0), .RST_USED(1'b0)) _TECHMAP_REPLACE_ (.D(D), .Q(Q), .CLK(C)); endmodule
module \$_DFFE_PP_ (input D, C, E, output Q); DFF #(.ENABLE_USED(1'b1), .RST_USED(1'b0)) _TECHMAP_REPLACE_ (.D(D), .Q(Q), .CLK(C), .EN(E)); endmodule
module \$_SDFF_PN0_ (input D, C, R, output Q); DFF #(.ENABLE_USED(1'b0), .RST_USED(1'b1)) _TECHMAP_REPLACE_ (.D(D), .Q(Q), .CLK(C), .RST_N(R)); endmodule
module \$_SDFFE_PN0P_ (input D, C, E, R, output Q); DFF #(.ENABLE_USED(1'b1), .RST_USED(1'b1)) _TECHMAP_REPLACE_ (.D(D), .Q(Q), .CLK(C), .EN(E), .RST_N(R)); endmodule
