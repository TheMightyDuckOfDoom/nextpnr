module \$lut (A, Y);
		parameter WIDTH = 0;
		parameter LUT = 0;
		input [WIDTH-1:0] A;
		output Y;

		localparam missing_bits = `LUT_K-WIDTH;
		localparam rep = 1 << missing_bits;

		wire [`LUT_K-1:0] lut_i;
	
		assign lut_i = {{missing_bits{A[WIDTH-1]}}, A};

		PCBFPGA_LUT #(
				.K    ( `LUT_K     ),
				.INIT ( {rep{LUT}} )
		) _TECHMAP_REPLACE_ (
				.I ( lut_i ),
				.F ( Y     )
		);
endmodule

module  \$_DFF_P_ (input D, C, output Q); 
		// No enable and no reset
		PCBFPGA_FF #(
				.HAS_RESET        ( 0 ),
				.ACTIVE_LOW_RESET ( 0 ),
				.NO_ENABLE        ( 1 )
		) _TECHMAP_REPLACE_ (
				.D   ( D  ),
				.Q   ( Q  ),
				.CLK ( C  )
		);
endmodule

module  \$_DFFE_PP_ (input D, C, E, output Q); 
		// No enable and no reset
		PCBFPGA_FF #(
				.HAS_RESET        ( 0 ),
				.ACTIVE_LOW_RESET ( 0 ),
				.NO_ENABLE        ( 0 )
		) _TECHMAP_REPLACE_ (
				.D   ( D  ),
				.Q   ( Q  ),
				.CLK ( C  ),
				.EN  ( E  )
		);
endmodule

module  \$_SDFF_PN0_ (input D, C, R, output Q); 
		// No enable and active low reset
		PCBFPGA_FF #(
				.HAS_RESET        ( 1 ),
				.ACTIVE_LOW_RESET ( 1 ),
				.NO_ENABLE        ( 1 )
		) _TECHMAP_REPLACE_ (
				.D   ( D ),
				.Q   ( Q ),
				.CLK ( C ),
				.RST ( R )
		);
endmodule

module  \$_SDFF_PP0_ (input D, C, R, output Q); 
		// No enable and active high reset
		PCBFPGA_FF #(
				.HAS_RESET        ( 1 ),
				.ACTIVE_LOW_RESET ( 0 ),
				.NO_ENABLE        ( 1 )
		) _TECHMAP_REPLACE_ (
				.D   ( D ),
				.Q   ( Q ),
				.CLK ( C ),
				.RST ( R )
		);
endmodule

module  \$_SDFFCE_PN0P_ (input D, C, R, E, output Q); 
		// Active high enable and active low reset
		PCBFPGA_FF #(
				.HAS_RESET        ( 1 ),
				.ACTIVE_LOW_RESET ( 1 ),
				.NO_ENABLE        ( 0 )
		) _TECHMAP_REPLACE_ (
				.D   ( D ),
				.Q   ( Q ),
				.CLK ( C ),
				.RST ( R ),
				.EN  ( E )
		);
endmodule

module  \$_SDFFCE_PP0P_ (input D, C, R, E, output Q); 
		// Active high enable and active high reset
		PCBFPGA_FF #(
				.HAS_RESET        ( 1 ),
				.ACTIVE_LOW_RESET ( 0 ),
				.NO_ENABLE        ( 0 )
		) _TECHMAP_REPLACE_ (
				.D   ( D ),
				.Q   ( Q ),
				.CLK ( C ),
				.RST ( R ),
				.EN  ( E )
		);
endmodule

module \ibuf (input i, output o);
	IOB #(.OUTPUT(0), .ENABLE_OUTPUT(0)) _TECHMAP_REPLACE_(.PAD(i), .O(o));
endmodule

module \obuf (output o, input i);
	IOB #(.OUTPUT(1), .ENABLE_OUTPUT(1)) _TECHMAP_REPLACE_(.PAD(o), .I(i));
endmodule

module \iobuft (output Y, input A, input E, inout PAD);
	IOB #(.OUTPUT(1), .ENABLE_OUTPUT(0)) _TECHMAP_REPLACE_(.PAD(PAD), .I(A), .EN(E), .O(Y));
endmodule

module \obuft (output Y, input A, input E);
	IOB #(.OUTPUT(1), .ENABLE_OUTPUT(0)) _TECHMAP_REPLACE_(.PAD(Y), .I(A), .EN(E));
endmodule

module \$_TBUF_ (output Y, input A, input E);
	IOB #(.OUTPUT(1), .ENABLE_OUTPUT(0)) _TECHMAP_REPLACE_(.PAD(Y), .I(A), .EN(E));
endmodule
