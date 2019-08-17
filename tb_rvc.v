`define assert(signal, value) if ((signal) !== (value)) begin $display("ASSERTION FAILED in %m: signal != value"); $finish(1); end

module tb_rvc();

   reg clk;
   reg rst;

   reg valid;
   
   wire ready;
   
   initial begin
      #1 clk = 0;
      #1 rst = 0;
      #1 valid = 0;
      
      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      #1 rst = 1;

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      #1 rst = 0;

      `assert(rdy.data, 1'b1)

      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      #1 valid = 1;
      `assert(rdy.data, 1'b1)

      
      #1 clk = 0;
      #1 clk = 1;
      #1 clk = 0;

      `assert(rdy.data, 1'b0)
      $display("Passed");
      
   end // initial begin

   always @(posedge clk) begin
      $display("ready_en = %d", rdy.en);
      
   end

   mod_register #(.WIDTH(1)) rdy(.clk(clk), .rst(rst));

   rvc dut(.clk(clk), .rst(rst), .ready_reg(rdy.in), .ready_en(rdy.en), .valid(valid));
   
endmodule
