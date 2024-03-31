module top (
  input clk_i,
  input [5:0] a_i,
  output d_o
);
  reg d_q;

  assign d_o = d_q;

  always @(posedge clk_i)
    d_q <= &a_i;
  
endmodule
