module optloop (a, b, c, d, y1, y2);
  input a, b, c, d;
  output y1, y2;
  assign y1 = ((a & b) & c) & d;
  assign y2 = ((a & c) & b) & d;
endmodule
