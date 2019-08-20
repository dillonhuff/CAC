`define assert(signal, value) if ((signal) !== (value)) begin $display("ASSERTION FAILED in %m: signal != value"); $finish(1); end

module tb_toggle();

  reg clk;
  reg rst;

  reg toggle = 1;
  wire value;

  initial begin

    #1 clk = 0;
    #1 rst = 0;
    #1 toggle = 0;

    #1 clk = 0;
    #1 clk = 1;
    #1 clk = 0;

    #1 rst = 1;

    #1 clk = 0;
    #1 clk = 1;
    #1 clk = 0;

    `assert(value, 1'b0)

    #1 clk = 0;
    #1 clk = 1;
    #1 clk = 0;

    `assert(value, 1'b0)

    #1 toggle = 1;

    #1 clk = 0;
    #1 clk = 1;
    #1 clk = 0;

    `assert(value, 1'b1)

    
    $display("Passed");
  end

  toggle dut(.clk(clk), .rst(rst), .value(value), .toggle(toggle));

endmodule
