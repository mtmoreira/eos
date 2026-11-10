// clk_ctrl: two-bit FSM (IDLE, LOAD, RUN, DONE) plus a two-bit
// run counter, sequencing the clk-domain datapath. RUN ends on
// the FSM's own counter, not on the divided domain, so no signal
// crosses between the two clock domains.
module clk_ctrl (
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
  reg [1:0] run_cnt;

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      state <= ST_IDLE;
      run_cnt <= 2'b00;
    end else begin
      case (state)
        ST_IDLE: if (start) state <= ST_LOAD;
        ST_LOAD: begin
          state <= ST_RUN;
          run_cnt <= 2'b00;
        end
        ST_RUN: begin
          if (run_cnt == 2'b11)
            state <= ST_DONE;
          else
            run_cnt <= run_cnt + 2'b01;
        end
        ST_DONE: state <= ST_IDLE;
        default: state <= ST_IDLE;
      endcase
    end
  end

  assign load = (state == ST_LOAD);
  assign run = (state == ST_RUN);
  assign done = (state == ST_DONE);
endmodule
