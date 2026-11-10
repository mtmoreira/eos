// clk_top: top of the F10 clocked demo design. Instantiates the
// five leaves below. One clock domain clk at the top port, one
// generated clock clk_div from the divide-by-2 toggle flop u_div.
// The domains are independent by construction: no registered
// signal crosses from one domain to the other. The clk-domain FSM
// ends RUN on its own counter, and the divided counter's terminal
// count goes only to the top output div_done.
module clk_top (
  input wire clk,
  input wire rst_n,
  input wire start,
  input wire [7:0] data,
  output wire done,
  output wire div_done,
  output wire [15:0] result
);
  wire div_q;
  wire run;
  wire load;
  wire div_tc;

  clk_div u_div (
    .clk(clk),
    .rst_n(rst_n),
    .q(div_q)
  );

  clk_ctrl u_ctrl (
    .clk(clk),
    .rst_n(rst_n),
    .start(start),
    .run(run),
    .load(load),
    .done(done)
  );

  clk_bank u_bank (
    .clk(clk),
    .rst_n(rst_n),
    .en(load),
    .data(data),
    .q(result[7:0])
  );

  clk_acc u_acc (
    .clk(clk),
    .rst_n(rst_n),
    .run(run),
    .load(load),
    .data(data),
    .q(result[15:8])
  );

  clk_cnt u_cnt (
    .clk(div_q),
    .rst_n(rst_n),
    .tc(div_tc)
  );

  // u_cap samples the input data on the divided clock. Its output
  // is intentionally unconnected: the top port list is fixed by
  // the design, and the capture register exists here as a clk_div
  // sink group for the clock network, not as a data output.
  clk_cap u_cap (
    .clk(div_q),
    .rst_n(rst_n),
    .en(1'b1),
    .data(data),
    .q()
  );

  assign div_done = div_tc;
endmodule
