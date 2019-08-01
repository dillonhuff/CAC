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

void addBinop(Context& c, const std::string& name, const int latency) {
  Module* const_1_1 = getConstMod(c, 1, 1);

  Module* add16 = c.addCombModule(name);
  add16->setPrimitive(true);
  add16->addInPort(16, "in0");
  add16->addInPort(16, "in1");
  add16->addOutPort(16, "out");

  assert(!add16->ept("in0").isOutput());  
  assert(!add16->ept("out").isInput);
  
  Module* add16Inv = c.addModule(name + "_apply");
  add16Inv->addInPort(16, "in0");
  add16Inv->addInPort(16, "in1");
  add16Inv->addOutPort(16, "out");    

  add16Inv->addOutPort(16, name + "_in0");
  add16Inv->addOutPort(16, name + "_in1");
  add16Inv->addInPort(16, name + "_out");

  assert(add16Inv->ept(name + "_in0").isOutput());  
  assert(add16Inv->ept(name + "_out").isInput);

  ModuleInstance* oneInst = add16Inv->addInstance(const_1_1, "one");
  
  CC* in0W =
    add16Inv->addStartInstruction(add16Inv->ipt("in0"), add16Inv->ipt(name + "_in0"));
  CC* in1W =
    add16Inv->addInstruction(add16Inv->ipt("in1"), add16Inv->ipt(name + "_in1"));
  CC* outW =
    add16Inv->addInstruction(add16Inv->ipt("out"), add16Inv->ipt(name + "_out"));

  in0W->continueTo(oneInst->pt("out"), in1W, 0);
  in1W->continueTo(oneInst->pt("out"), outW, 0);

  add16->addAction(add16Inv);
}

int main() {

  Context c;

  addBinop(c, "add16", 0);
  
  // Module* const_1_1 = c.addCombModule("const_1_1");
  // const_1_1->setPrimitive(true);
  // const_1_1->addOutPort(1, "out");
  
  // Module* add16 = c.addCombModule("add16");
  // add16->setPrimitive(true);
  // add16->addInPort(16, "in0");
  // add16->addInPort(16, "in1");
  // add16->addOutPort(16, "out");

  // assert(!add16->ept("in0").isOutput());  
  // assert(!add16->ept("out").isInput);
  
  // Module* add16Inv = c.addModule("add16_apply");
  // add16Inv->addInPort(16, "in0");
  // add16Inv->addInPort(16, "in1");
  // add16Inv->addOutPort(16, "out");    

  // add16Inv->addOutPort(16, "adder_in0");
  // add16Inv->addOutPort(16, "adder_in1");
  // add16Inv->addInPort(16, "adder_out");

  // assert(add16Inv->ept("adder_in0").isOutput());  
  // assert(add16Inv->ept("adder_out").isInput);

  // ModuleInstance* oneInst = add16Inv->addInstance(const_1_1, "one");
  
  // CC* in0W =
  //   add16Inv->addStartInstruction(add16Inv->ipt("in0"), add16Inv->ipt("adder_in0"));
  // CC* in1W =
  //   add16Inv->addInstruction(add16Inv->ipt("in1"), add16Inv->ipt("adder_in1"));
  // CC* outW =
  //   add16Inv->addInstruction(add16Inv->ipt("out"), add16Inv->ipt("adder_out"));

  // in0W->continueTo(oneInst->pt("out"), in1W, 0);
  // in1W->continueTo(oneInst->pt("out"), outW, 0);

  // cout << "add16 inv" << endl;
  // cout << *add16Inv << endl;
  
  // add16->addAction(add16Inv);

  Module* add16 = c.getModule("add16");
  Module* add16Inv = add16->action("add16_apply");

  Module* addWrapper = c.addModule("add_16_wrapper");
  addWrapper->addInPort(16, "in0");
  addWrapper->addInPort(16, "in1");
  addWrapper->addOutPort(16, "out");
  
  auto mAdd = addWrapper->addInstance(add16, "adder");

  CC* callAdd = addWrapper->addInvokeInstruction(add16Inv);
  callAdd->setIsStartAction(true);
  
  callAdd->bind("add16_in0", mAdd->pt("in0"));
  callAdd->bind("add16_in1", mAdd->pt("in1"));
  callAdd->bind("add16_out", mAdd->pt("out"));

  callAdd->bind("in0", addWrapper->ipt("in0"));
  callAdd->bind("in1", addWrapper->ipt("in1"));
  callAdd->bind("out", addWrapper->ipt("out"));    

  cout << "Add wrapper before lowering" << endl;
  cout << *addWrapper << endl;

  inlineInvokes(addWrapper);

  cout << "Add wrapper after lowering" << endl;
  cout << *addWrapper << endl;
  
  emitVerilog(c, addWrapper);

  runCmd("iverilog -o tb tb_add_16_wrapper.v add_16_wrapper.v builtins.v");

  
  // Next: two pipelined adders with a valid controller?
  // send the add output to a signal?
  // signals, connections, and instructions?

  // runCmd("clang -S -emit-llvm ./c_files/read_write_ram.c -c -O3");

  // Context c;
  // loadLLVMFromFile(c, "read_write_ram", "./read_write_ram.ll");

  // Module* m = c.getModule("read_write_ram");
  // assert(m != nullptr);

  // cout << "Final module" << endl;
  // cout << *m << endl;

  // emitVerilog(c, m);

}
