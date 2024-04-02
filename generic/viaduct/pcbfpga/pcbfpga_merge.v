module obuft (
  input A,
  input E,
  output Y
);
  wire tbuf_to_iob;

  $_TBUF_ i_tbuf (
    .E(E),
    .A(A),
    .Y(tbuf_to_iob)
  );

  obuf i_obuf (
    .i(tbuf_to_iob),
    .o(Y)
  );
endmodule
