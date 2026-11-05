// hier_ctrl: four-state FSM sequencing the datapath.
// IDLE waits for start, LOAD clears the accumulator, RUN steps the
// datapath while the leaf counter runs, DONE pulses for one cycle
// and returns to IDLE.
module hier_ctrl (
  input wire clk,
  input wire rst_n,
  input wire start,
  output wire run,
  output wire load,
  output wire done
);
  localparam [1:0] ST_IDLE = 2'b00;
  localparam [1:0] ST_LOAD = 2'b01;
  localparam [1:0] ST_RUN  = 2'b10;
  localparam [1:0] ST_DONE = 2'b11;

  reg [1:0] state;
  wire tc;

  hier_counter u_cnt (
    .clk(clk),
    .rst_n(rst_n),
    .en(run),
    .tc(tc)
  );

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      state <= ST_IDLE;
    else begin
      case (state)
        ST_IDLE: if (start) state <= ST_LOAD;
        ST_LOAD: state <= ST_RUN;
        ST_RUN: if (tc) state <= ST_DONE;
        ST_DONE: state <= ST_IDLE;
        default: state <= ST_IDLE;
      endcase
    end
  end

  assign load = (state == ST_LOAD);
  assign run = (state == ST_RUN);
  assign done = (state == ST_DONE);
endmodule
