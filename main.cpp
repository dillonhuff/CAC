#include "llvm_loader.h"

#include <fstream>

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

bool runIVerilogTB(const std::string& moduleName) {
  string mainName = "tb_" + moduleName + ".v";
  string modFile = moduleName + ".v";

  string genCmd = "iverilog -g2005 -o " + moduleName + " " + mainName + " " + modFile + " builtins.v RAM.v delay.v";

  runCmd(genCmd);

  string resFile = moduleName + "_tb_result.txt";
  string exeCmd = "./" + moduleName + " > " + resFile;
  runCmd(exeCmd);

  ifstream res(resFile);
  std::string str((std::istreambuf_iterator<char>(res)),
                  std::istreambuf_iterator<char>());

  cout << "str = " << str << endl;
    
  //runCmd("rm -f " + resFile);

  reverse(begin(str), end(str));
  string lastLine;

  for (int i = 1; i < (int) str.size(); i++) {
    if (str[i] == '\n') {
      break;
    }

    lastLine += str[i];
  }

  reverse(begin(lastLine), end(lastLine));

  cout << "Lastline = " << lastLine << endl;
  return lastLine == "Passed";
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

    // cout << "Add wrapper before lowering" << endl;
    // cout << *addWrapper << endl;

    inlineInvokes(addWrapper);

    // cout << "Add wrapper after lowering" << endl;
    // cout << *addWrapper << endl;
  
    emitVerilog(c, addWrapper);

    assert(runIVerilogTB("add_16_wrapper"));
    
    // runCmd("iverilog -o tb tb_add_16_wrapper.v add_16_wrapper.v builtins.v");
    // runCmd("./tb >& res.txt");

    // ifstream t("res.txt");
    // std::string str((std::istreambuf_iterator<char>(t)),
    //                 std::istreambuf_iterator<char>());
    
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
    
    // cout << "Two adds..." << endl;
    // cout << *pipeAdds << endl;

    inlineInvokes(pipeAdds);
    deleteDeadResources(pipeAdds);
    // cout << "Add wrapper after lowering" << endl;
    // cout << *pipeAdds << endl;
  
    emitVerilog(c, pipeAdds);
    assert(runIVerilogTB(pipeAdds->getName()));
    //runCmd("iverilog -o tb tb_pipelined_adds.v pipelined_adds.v builtins.v");
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
    //Module* w16 = getWireMod(c, 16);
    Module* chan16 = getChannelMod(c, 16);

    Module* pipeAdds = c.addModule("channel_pipelined_adds");
    pipeAdds->addInPort(1, "in_valid");
    pipeAdds->addInPort(16, "in_data");
    pipeAdds->addOutPort(16, "result");

    ModuleInstance* oneInst = pipeAdds->addInstance(const_1_1, "one");
    auto add1 = pipeAdds->addInstance(add16, "add1");
    auto add2 = pipeAdds->addInstance(add16, "add2");
    //auto add1Wire = pipeAdds->addInstance(w16, "add1Wire");
    auto c16 = pipeAdds->addInstance(one16, "n16");
    auto chan = pipeAdds->addInstance(chan16, "pipe_channel");

    // On start: If valid == 1 then transition to firstAdd?

    CC* entryCheck = pipeAdds->addEmptyInstruction();
    entryCheck->setIsStartAction(true);

    CC* firstAdd = pipeAdds->addInvokeInstruction(add16Apply);
    firstAdd->bind("in0", pipeAdds->ipt("in_data"));
    firstAdd->bind("in1", c16->pt("out"));
    firstAdd->bind("out", chan->pt("in"));

    firstAdd->bind("add16_in0", add1->pt("in0"));
    firstAdd->bind("add16_in1", add1->pt("in1"));
    firstAdd->bind("add16_out", add1->pt("out"));
    
    CC* secondAdd = pipeAdds->addInvokeInstruction(add16Apply);

    // Bind to signal input?
    secondAdd->bind("in0", chan->pt("out"));
    secondAdd->bind("in1", c16->pt("out"));
    secondAdd->bind("out", pipeAdds->ipt("result"));

    secondAdd->bind("add16_in0", add2->pt("in0"));
    secondAdd->bind("add16_in1", add2->pt("in1"));
    secondAdd->bind("add16_out", add2->pt("out"));

    entryCheck->continueTo(oneInst->pt("out"), entryCheck, 1);
    entryCheck->continueTo(pipeAdds->ipt("in_valid"), firstAdd, 0);

    firstAdd->continueTo(oneInst->pt("out"), secondAdd, 1);
    
    // cout << "Two adds..." << endl;
    // cout << *pipeAdds << endl;

    inlineInvokes(pipeAdds);
    synthesizeChannels(pipeAdds);
    deleteDeadResources(pipeAdds);
    
    // cout << "Add wrapper after lowering" << endl;
    // cout << *pipeAdds << endl;
  
    emitVerilog(c, pipeAdds);
    assert(runIVerilogTB(pipeAdds->getName()));
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
    //Module* w16 = getWireMod(c, 16);
    Module* chan16 = getChannelMod(c, 16);

    Module* pipeAdds = c.addModule("structure_reduce_channel_pipelined_adds");
    pipeAdds->addInPort(1, "in_valid");
    pipeAdds->addInPort(16, "in_data");
    pipeAdds->addOutPort(16, "result");

    ModuleInstance* oneInst = pipeAdds->addInstance(const_1_1, "one");
    auto add1 = pipeAdds->addInstance(add16, "add1");
    auto add2 = pipeAdds->addInstance(add16, "add2");
    //auto add1Wire = pipeAdds->addInstance(w16, "add1Wire");
    auto c16 = pipeAdds->addInstance(one16, "n16");
    auto chan = pipeAdds->addInstance(chan16, "pipe_channel");

    // On start: If valid == 1 then transition to firstAdd?

    CC* entryCheck = pipeAdds->addEmptyInstruction();
    entryCheck->setIsStartAction(true);

    CC* firstAdd = pipeAdds->addInvokeInstruction(add16Apply);
    firstAdd->bind("in0", pipeAdds->ipt("in_data"));
    firstAdd->bind("in1", c16->pt("out"));
    firstAdd->bind("out", chan->pt("in"));

    firstAdd->bind("add16_in0", add1->pt("in0"));
    firstAdd->bind("add16_in1", add1->pt("in1"));
    firstAdd->bind("add16_out", add1->pt("out"));
    
    CC* secondAdd = pipeAdds->addInvokeInstruction(add16Apply);

    // Bind to signal input?
    secondAdd->bind("in0", chan->pt("out"));
    secondAdd->bind("in1", c16->pt("out"));
    secondAdd->bind("out", pipeAdds->ipt("result"));

    secondAdd->bind("add16_in0", add2->pt("in0"));
    secondAdd->bind("add16_in1", add2->pt("in1"));
    secondAdd->bind("add16_out", add2->pt("out"));

    entryCheck->continueTo(oneInst->pt("out"), entryCheck, 1);
    entryCheck->continueTo(pipeAdds->ipt("in_valid"), firstAdd, 0);

    firstAdd->continueTo(oneInst->pt("out"), secondAdd, 1);
    
    // cout << "Two adds..." << endl;
    // cout << *pipeAdds << endl;

    inlineInvokes(pipeAdds);
    synthesizeChannels(pipeAdds);
    reduceStructures(pipeAdds);
    deleteDeadResources(pipeAdds);
    
    // cout << "Add wrapper after lowering" << endl;
    // cout << *pipeAdds << endl;
  
    emitVerilog(c, pipeAdds);
    assert(runIVerilogTB(pipeAdds->getName()));
  }

  {
    runCmd("clang -S -emit-llvm ./c_files/read_write_ram.c -c -O3");

    Context c;
    loadLLVMFromFile(c, "read_write_ram", "./read_write_ram.ll");

    Module* m = c.getModule("read_write_ram");
    assert(m != nullptr);

    cout << "Final module" << endl;
    cout << *m << endl;

    inlineInvokes(m);
    synthesizeDelays(m);
    synthesizeChannels(m);
    reduceStructures(m);
    deleteNoEffectInstructions(m);
    
    emitVerilog(c, m);
    assert(runIVerilogTB(m->getName()));
  }

  {
    runCmd("clang -S -emit-llvm ./c_files/read_add_2_ram.c -c -O3");

    Context c;
    loadLLVMFromFile(c, "read_add_2_ram", "./read_add_2_ram.ll");

    Module* m = c.getModule("read_add_2_ram");
    assert(m != nullptr);

    cout << "Final module" << endl;
    cout << *m << endl;

    inlineInvokes(m);
    synthesizeDelays(m);
    deleteNoEffectInstructions(m);        
    synthesizeChannels(m);
    reduceStructures(m);
    deleteNoEffectInstructions(m);    
    deleteDeadResources(m);

    emitVerilog(c, m);
    assert(runIVerilogTB(m->getName()));
  }

  {
    runCmd("clang -S -emit-llvm ./c_files/read_add_2_or_3.c -c -O3");

    Context c;
    loadLLVMFromFile(c, "read_add_2_or_3", "./read_add_2_or_3.ll");

    Module* m = c.getModule("read_add_2_or_3");
    assert(m != nullptr);

    cout << "Final module" << endl;
    cout << *m << endl;

    inlineInvokes(m);
    synthesizeDelays(m);
    deleteNoEffectInstructions(m);    
    synthesizeChannels(m);
    reduceStructures(m);
    deleteNoEffectInstructions(m);
    deleteDeadResources(m);
    deleteNoEffectInstructions(m);
    
    emitVerilog(c, m);
    assert(runIVerilogTB(m->getName()));
    
  }

  // {
  //   runCmd("clang -S -emit-llvm ./c_files/read_add_2_loop.c -c -O3");

  //   Context c;
  //   loadLLVMFromFile(c, "read_add_2_loop", "./read_add_2_loop.ll");

  //   Module* m = c.getModule("read_add_2_loop");
  //   assert(m != nullptr);

  //   cout << "Final module" << endl;
  //   cout << *m << endl;

  //   inlineInvokes(m);
  //   synthesizeDelays(m);
  //   synthesizeChannels(m);
  //   reduceStructures(m);

  //   emitVerilog(c, m);
  //   assert(runIVerilogTB(m->getName()));
  // }

  // TODO:
  //  1. Set reset values of sensitive ports to their defaults
  
}
