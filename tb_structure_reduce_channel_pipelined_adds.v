`define assert(signal, value) if ((signal) !== (value)) begin $display("ASSERTION FAILED in %m: signal != value"); $finish(1); end

module test();

   reg clk;
   reg rst;
   reg [15:0] in;
   reg valid;
   wire [15:0] result;

   initial begin
      #1 clk = 0;
      #1 rst = 0;

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;                  
      
      #1 rst = 1;

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;                  

      #1 rst = 0;
      
      #1 in = 10;
      #1 valid = 1;
      
      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;                  

      #1 in = 15;
      $display("result = %d", result);

      `assert(result, 12)
      

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      $display("result = %d", result);

      `assert(result, 17)
      #1 valid = 0;
      #1 in = 18;      
      
      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      $display("invalid result = %d", result);
      `assert(result, 0)

      #10

      $display("Passed");
   end

   structure_reduce_channel_pipelined_adds dut(.clk(clk), .rst(rst), .result(result), .in_valid(valid), .in_data(in));
   
endmodule
