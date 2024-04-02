module top (
  input clk_i,
  input [3:0] a_i,
  output c_o,
  output [1:0] d_o,
  output e_io
);
  reg [1:0] d_q;

  assign c_o = &a_i[3:1];
  assign d_o = d_q;
  assign e_io = a_i[0] ? d_q[1] : 1'bZ;

  always @(posedge clk_i) begin
    d_q[0] <= |a_i[3:0];
    d_q[1] <= d_q[0];
  end
  
endmodule
