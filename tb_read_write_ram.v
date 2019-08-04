`define assert(signal, value) if ((signal) !== (value)) begin $display("ASSERTION FAILED in %m: signal != value"); $finish(1); end

module test();


   reg clk;
   reg rst;
   reg start;
   wire done;
   wire ready;
   
   initial begin
      #1 clk = 0;
      #1 rst = 0;
      #1 start = 0;

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      #1 rst = 1;
      

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      `assert(ready, 1'b1)

      #1 rst = 0;

      $display("Passed");
      
   end

   read_write_ram dut(.clk(clk),
                      .rst(rst),
                      .ready(ready),
                      .start(start),
                      .done(done));
   
   
endmodule
