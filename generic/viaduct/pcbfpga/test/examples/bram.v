module ram #(
    parameter DATA_WIDTH = 4,
    parameter ADDR_WIDTH = 14,
    parameter DUAL_PORT  = 0,
    parameter SYNC_READ  = 1
)(
    input  CLK, WE,
    input  [ADDR_WIDTH-1:0] ADDR, ADDR2,
    input  [DATA_WIDTH-1:0] DIN,
    output reg [DATA_WIDTH-1:0] DOUT
);
    reg [DATA_WIDTH-1:0] mem [0:(1 << ADDR_WIDTH)-1];

    always @(posedge CLK) begin
        if(WE) mem[ADDR] <= DIN;
        if(SYNC_READ) DOUT <= DUAL_PORT ? mem[ADDR2] : mem[ADDR];
    end

    if(!SYNC_READ) begin
        assign DOUT = DUAL_PORT ? mem[ADDR2] : mem[ADDR];
    end
endmodule
