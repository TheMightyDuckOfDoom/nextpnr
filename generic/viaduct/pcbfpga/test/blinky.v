module blinky(input rst, input clk, output reg [7:0] leds);

reg [7:0] ctr;
always @(posedge clk) begin
	if (rst)
		ctr <= '0;
	else
		ctr <= ctr + 'd1;
end

assign leds = {ctr[7:4], ctr[7:4]};

endmodule
