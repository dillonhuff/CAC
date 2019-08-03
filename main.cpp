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

void addBinop(Context& c, const std::string& name, const int cycleLatency) {
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
  in1W->continueTo(oneInst->pt("out"), outW, cycleLatency);

  add16->addAction(add16Inv);
}

int main() {

  {
    Context c;

    addBinop(c, "add16", 0);

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
  }

  {
    Context c;
    addBinop(c, "add16", 0);
    
    Module* add16 = c.getModule("add16");
    Module* add16Apply = c.getModule("add16_apply");
    Module* one16 = getConstMod(c, 16, 1);
    Module* const_1_1 = getConstMod(c, 1, 1);
    Module* w16 = getWireMod(c, 16);
    Module* reg16 = getRegMod(c, 16);    

    Module* pipeAdds = c.addModule("pipelined_adds");
    pipeAdds->addInPort(1, "in_valid");
    pipeAdds->addInPort(16, "in_data");
    pipeAdds->addOutPort(16, "result");

    ModuleInstance* oneInst = pipeAdds->addInstance(const_1_1, "one");
    auto add1 = pipeAdds->addInstance(add16, "add1");
    auto add2 = pipeAdds->addInstance(add16, "add2");
    auto add1Wire = pipeAdds->addInstance(w16, "add1Wire");
    auto c16 = pipeAdds->addInstance(one16, "n16");
    auto r16 = pipeAdds->addInstanceSeq(reg16, "storage");

    // On start: If valid == 1 then transition to firstAdd?

    CC* entryCheck = pipeAdds->addEmptyInstruction();
    entryCheck->setIsStartAction(true);

    CC* firstAdd = pipeAdds->addInvokeInstruction(add16Apply);
    firstAdd->bind("in0", pipeAdds->ipt("in_data"));
    firstAdd->bind("in1", c16->pt("out"));
    firstAdd->bind("out", add1Wire->pt("in"));

    firstAdd->bind("add16_in0", add1->pt("in0"));
    firstAdd->bind("add16_in1", add1->pt("in1"));
    firstAdd->bind("add16_out", add1->pt("out"));

    CC* storeFirstRes =
      pipeAdds->addInvokeInstruction(reg16->action("reg_16_st"));
    storeFirstRes->bind("reg_16_in", r16->pt("in"));
    storeFirstRes->bind("reg_16_en", r16->pt("en"));    

    storeFirstRes->bind("in", add1Wire->pt("out"));
    storeFirstRes->bind("en", oneInst->pt("out"));
    
    CC* secondAdd = pipeAdds->addInvokeInstruction(add16Apply);
    // Could transform this into a wire bound to an invocation of register load...
    secondAdd->bind("in0", r16->pt("data")); //add1Wire->pt("out"));
    secondAdd->bind("in1", c16->pt("out"));
    secondAdd->bind("out", pipeAdds->ipt("result"));

    secondAdd->bind("add16_in0", add2->pt("in0"));
    secondAdd->bind("add16_in1", add2->pt("in1"));
    secondAdd->bind("add16_out", add2->pt("out"));

    entryCheck->continueTo(oneInst->pt("out"), entryCheck, 1);
    entryCheck->continueTo(pipeAdds->ipt("in_valid"), firstAdd, 0);

    firstAdd->continueTo(oneInst->pt("out"), secondAdd, 1);
    firstAdd->continueTo(oneInst->pt("out"), storeFirstRes, 0);    
    // Also continue to sending registers?
    
    cout << "Two adds..." << endl;
    cout << *pipeAdds << endl;

    inlineInvokes(pipeAdds);

    cout << "Add wrapper after lowering" << endl;
    cout << *pipeAdds << endl;
  
    emitVerilog(c, pipeAdds);
    runCmd("iverilog -o tb tb_pipelined_adds.v pipelined_adds.v builtins.v");
  }

  {
    // Now: Example of signals
    //  - Implement two pipelined adders with signal between them instead of
    //    an explicit register
    Context c;
    addBinop(c, "add16", 0);
    
    Module* add16 = c.getModule("add16");
    Module* add16Apply = c.getModule("add16_apply");
    Module* one16 = getConstMod(c, 16, 1);
    Module* const_1_1 = getConstMod(c, 1, 1);
    Module* w16 = getWireMod(c, 16);
    Module* chan16 = getChannelMod(c, 16);

    Module* pipeAdds = c.addModule("channel_pipelined_adds");
    pipeAdds->addInPort(1, "in_valid");
    pipeAdds->addInPort(16, "in_data");
    pipeAdds->addOutPort(16, "result");

    ModuleInstance* oneInst = pipeAdds->addInstance(const_1_1, "one");
    auto add1 = pipeAdds->addInstance(add16, "add1");
    auto add2 = pipeAdds->addInstance(add16, "add2");
    auto add1Wire = pipeAdds->addInstance(w16, "add1Wire");
    auto c16 = pipeAdds->addInstance(one16, "n16");
    auto chan = pipeAdds->addInstance(chan16, "pipe_channel");

    // On start: If valid == 1 then transition to firstAdd?

    CC* entryCheck = pipeAdds->addEmptyInstruction();
    entryCheck->setIsStartAction(true);

    CC* firstAdd = pipeAdds->addInvokeInstruction(add16Apply);
    firstAdd->bind("in0", pipeAdds->ipt("in_data"));
    firstAdd->bind("in1", c16->pt("out"));
    firstAdd->bind("out", chan->pt("in")); //add1Wire->pt("in"));

    firstAdd->bind("add16_in0", add1->pt("in0"));
    firstAdd->bind("add16_in1", add1->pt("in1"));
    firstAdd->bind("add16_out", add1->pt("out"));

    // CC* storeFirstRes =
    //   pipeAdds->addInvokeInstruction(reg16->action("reg_16_st"));
    // storeFirstRes->bind("reg_16_in", r16->pt("in"));
    // storeFirstRes->bind("reg_16_en", r16->pt("en"));    

    // storeFirstRes->bind("in", add1Wire->pt("out"));
    // storeFirstRes->bind("en", oneInst->pt("out"));
    
    CC* secondAdd = pipeAdds->addInvokeInstruction(add16Apply);

    // Bind to signal input?
    // secondAdd->bind("in0", r16->pt("data")); //add1Wire->pt("out"));
    secondAdd->bind("in0", chan->pt("out"));
    secondAdd->bind("in1", c16->pt("out"));
    secondAdd->bind("out", pipeAdds->ipt("result"));

    secondAdd->bind("add16_in0", add2->pt("in0"));
    secondAdd->bind("add16_in1", add2->pt("in1"));
    secondAdd->bind("add16_out", add2->pt("out"));

    entryCheck->continueTo(oneInst->pt("out"), entryCheck, 1);
    entryCheck->continueTo(pipeAdds->ipt("in_valid"), firstAdd, 0);

    firstAdd->continueTo(oneInst->pt("out"), secondAdd, 1);
    //firstAdd->continueTo(oneInst->pt("out"), storeFirstRes, 0);    
    // Also continue to sending registers?
    
    cout << "Two adds..." << endl;
    cout << *pipeAdds << endl;

    inlineInvokes(pipeAdds);
    synthesizeChannels(pipeAdds);

    cout << "Add wrapper after lowering" << endl;
    cout << *pipeAdds << endl;
  
    emitVerilog(c, pipeAdds);
    runCmd("iverilog -o tb tb_channel_pipelined_adds.v channel_pipelined_adds.v builtins.v");
  }

  // Once the signals example is working?
  // - RTL elaboration, start with simplifying single connection, insensitive
  //   module ports
  // - Add default values
  // - LLVM backend
  
  // runCmd("clang -S -emit-llvm ./c_files/read_write_ram.c -c -O3");

  // Context c;
  // loadLLVMFromFile(c, "read_write_ram", "./read_write_ram.ll");

  // Module* m = c.getModule("read_write_ram");
  // assert(m != nullptr);

  // cout << "Final module" << endl;
  // cout << *m << endl;

  // emitVerilog(c, m);

}
