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

      // #1 clk = 0;
      // #1 clk = 1;
      // #1 clk = 0;
      
      // #1 rst = 0;

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      $display("after rst done = %d", done);
      $display("after rst ready = %d", ready);      
      
      `assert(ready, 1'b1)
      `assert(done, 1'b0)

      #1 rst = 0;

      #1 start = 1;
      
      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      #1 start = 0;

      `assert(ready, 1'b0)            
      
      // 1
      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      // 2

      `assert(ready, 1'b0)            

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      // 3

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;
      
      $display("At end ready = %d", ready);
      $display("At end done  = %d", done);
      $display("Start        = %d", start);
      
      `assert(done, 1'b1)
      `assert(ready, 1'b1)      

      $display("Passed");
      
   end // initial begin

   RAM ram(.clk(clk), .rst(rst));
   

   read_write_ram dut(.clk(clk),
                      .rst(rst),
                      .ready(ready),
                      .start(start),
                      .done(done),

                      .ram_raddr_0(ram.raddr_0),
                      .ram_rdata_0(ram.rdata_0),

                      .ram_waddr_0(ram.waddr_0),
                      .ram_wen_0(ram.wen_0),
                      .ram_wdata_0(ram.wdata_0));
   
   
endmodule
