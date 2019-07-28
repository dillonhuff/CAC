#include "llvm_loader.h"

// Example: An adder module has one action, which takes
// an adder as its first argument, and which

// Note: Convert to behavioral code by pushing
// transition conditions across edges in the program
// and adding false transitions to non-transfer blocks

using namespace CAC;

void runCmd(const std::string& cmd) {
  cout << "Running command " << cmd << endl;
  int res = system(cmd.c_str());
  assert(res == 0);
}

int main() {

  Module* const_1_1 = new Module("const_1_1");
  const_1_1->setPrimitive(true);
  const_1_1->addOutPort(1, "out");
  
  Module* wire16 = new Module("wire16");
  wire16->setPrimitive(true);
  wire16->addInPort(16, "in");
  wire16->addOutPort(16, "out");  
  
  Module* add16 = new Module("add16");
  add16->setPrimitive(true);
  add16->addInPort(16, "in0");
  add16->addInPort(16, "in1");
  add16->addOutPort(16, "out");

  Module* add16Inv = new Module("add16Inv");

  ModuleInstance* oneInst = add16Inv->addInstance(const_1_1, "one");

  ModuleInstance* in0 = add16Inv->addInstance(wire16, "in0");
  ModuleInstance* in1 = add16Inv->addInstance(wire16, "in1");
  ModuleInstance* out = add16Inv->addInstance(wire16, "out");

  ModuleInstance* ain0 = add16Inv->addInstance(wire16, "adder_in0");
  ModuleInstance* ain1 = add16Inv->addInstance(wire16, "adder_in1");
  ModuleInstance* aout = add16Inv->addInstance(wire16, "adder_out");
  
  CC* in0W = add16Inv->addStartInstruction(in0->pt("out"), ain0->pt("in"));
  CC* in1W = add16Inv->addInstruction(in1->pt("out"), ain1->pt("in"));
  CC* outW = add16Inv->addInstruction(out->pt("in"), aout->pt("out"));

  in0W->continueTo(oneInst->pt("out"), in0W, 0);
  in1W->continueTo(oneInst->pt("out"), outW, 0);
  
  add16->addAction(add16Inv);

  runCmd("clang -S -emit-llvm ./c_files/read_write_ram.c -c -O3");

  Context c;
  loadLLVMFromFile(c, "read_write_ram", "./read_write_ram.ll");

  Module* m = c.getModule("read_write_ram");
  assert(m != nullptr);

  cout << "Final module" << endl;
  cout << *m << endl;

  emitVerilog(c, m);

}
