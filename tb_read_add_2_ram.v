`define assert(signal, value) if ((signal) !== (value)) begin $display("ASSERTION FAILED in %m: signal != value"); $finish(1); end

module test();


   reg clk;
   reg rst;
   reg start;
   wire done;
   wire ready;

   reg  debug_write_en;
   reg [31:0] debug_write_data;
   reg [31:0] debug_write_addr;

   wire [31:0] debug_read_data;
   reg [31:0] debug_read_addr;
   
   initial begin
      #1 debug_write_addr = 10;
      #1 debug_write_data = 15;
      #1 debug_write_en = 1;

      #1 debug_read_addr = 12;
      
      #1 clk = 0;
      #1 rst = 0;
      #1 start = 0;

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      #1 debug_write_en = 0;

      
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

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;
      
      $display("At end ready = %d", ready);
      $display("At end done  = %d", done);
      $display("Start        = %d", start);
      $display("ram[12]      = %d", debug_read_data);      

      `assert(done, 1'b1)
      `assert(ready, 1'b1)
      `assert(debug_read_data, 17)      

      $display("Passed");
      
   end // initial begin

   RAM ram(.clk(clk),
           .rst(rst),

           .debug_data(debug_read_data),
           .debug_addr(debug_read_addr),           

           .debug_write_data(debug_write_data),
           .debug_write_en(debug_write_en),
           .debug_write_addr(debug_write_addr));
   

   read_add_2_ram dut(.clk(clk),
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
