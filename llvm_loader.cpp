#include "llvm_loader.h"

#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

using namespace llvm;
using namespace CAC;

string calledFuncName(llvm::Instruction* const iptr) {
  assert(CallInst::classof(iptr));

  CallInst* call = dyn_cast<CallInst>(iptr);
  Function* called = call->getCalledFunction();

  string name = called->getName();
  return name;
}

bool matchesCall(std::string str, llvm::Instruction* const iptr) {
  if (!CallInst::classof(iptr)) {
    return false;
  }

  CallInst* call = dyn_cast<CallInst>(iptr);
  Function* called = call->getCalledFunction();

  string name = called->getName();

  if (hasPrefix(name, str)) {
    return true;
  }
  return false;

}

std::string typeString(Type* const tptr) {
  std::string str;
  llvm::raw_string_ostream ss(str);
  ss << *tptr;

  return ss.str();
}

std::string valueString(const Value* const iptr) {
  assert(iptr != nullptr);
    
  std::string str;
  llvm::raw_string_ostream ss(str);
  ss << *iptr;

  return ss.str();
}

void addRAM32Primitive(Context& c) {
  CAC::Module* m = c.addModule("ram_32_128");

  CAC::Module* rd = c.addModule("ram_32_128_read");
  CAC::Module* wr = c.addModule("ram_32_128_write");

  m->addAction(rd);
  m->addAction(wr);  
}

CC* setReg(ModuleInstance* r, const int value, CAC::Module* container) {
  auto setR =
    container->addInvokeInstruction(r->action("st"));
  bindByType(setR, r);
  setR->bind("in", container->c(r->pt("in").getWidth(), value));
  setR->bind("en", container->c(1, 1));

  return setR;
}

Port notVal(const Port toNegate, CAC::Module* m) {
  auto nm = getNotMod(*(m->getContext()), 1);
  auto notI = m->freshInstance(nm, "not");
  auto notAct = notI->action("apply");

  auto resWire =
    m->freshInstance(getWireMod(*(m->getContext()), toNegate.getWidth()),
                     "not_res");

  auto notActInv = m->addInvokeInstruction(notAct);
  bindByType(notActInv, notI);
  notActInv->bind("in", toNegate);
  notActInv->bind("out", resWire->pt("in"));  

  return resWire->pt("out");
}

// Maybe better way to translate LLVM?
//  1. Create channels for all non-pointer values
//  2. Create registers for all pointers to non-builtins
void loadLLVMFromFile(Context& c,
                      const std::string& topFunction,
                      const std::string& filePath) {


  
  map<string, CAC::Module*> builtinModDefs;
  CAC::Module* ram32_128 = c.addModule("ram32_128");
  ram32_128->setPrimitive(true);
  ram32_128->addInPort(32, "raddr");
  ram32_128->addOutPort(32, "rdata");
  ram32_128->addInPort(1, "wen");  
  ram32_128->addInPort(32, "waddr");
  ram32_128->addInPort(32, "wdata");
  ram32_128->setDefaultValue("wen", 0);

  CAC::Module* read = c.addModule("ram32_128_read");
  read->addOutPort(32, "ram32_128_raddr");
  read->addInPort(32, "ram32_128_rdata");  

  read->addInPort(32, "raddr");
  read->addOutPort(32, "rdata");  

  CC* setRaddr = read->addStartInstruction(read->ipt("raddr"),
                                           read->ipt("ram32_128_raddr"));
  // CC* readRdata = read->addInstruction(ramData->pt("in"),
  //                                      resRdata->pt("in"));
  // setRaddr->continueTo(read->constOut(1, 1), readRdata, 1);
  
  CAC::Module* write = c.addModule("ram32_128_write");

  ram32_128->addAction(read);
  ram32_128->addAction(write);
  
  builtinModDefs["struct.ram_32_128"] = ram32_128;
  builtinModDefs["read"] = read;
  builtinModDefs["write"] = write;
  
  SMDiagnostic err;
  LLVMContext context;

  std::unique_ptr<llvm::Module> mod(parseIRFile(filePath, err, context));
  if (!mod) {
    outs() << "Error: No mod\n";
    assert(false);
  }

  cout << "Loaded module" << endl;
  Function* f = mod->getFunction(topFunction);

  cout << "Converting function" << endl;
  cout << valueString(f);

  CAC::Module* m = c.addModule(topFunction);
  addRAM32Primitive(c);

  CAC::Module* mCall = c.addModule(topFunction + "_call");

  map<Argument*, CAC::Module*> argTypes;

  // Calling convention ports
  m->addOutPort(1, "ready");
  m->addInPort(1, "start");
  m->addOutPort(1, "done");

  // Calling convention registers
  auto readyReg = m->freshReg(1, "ready");
  m->addSC(m->ipt("ready"), readyReg->pt("data"));

  auto doneReg = m->freshReg(1, "done");
  m->addSC(m->ipt("done"), doneReg->pt("data"));

  // Entry instruction
  auto entry = m->addEmptyInstruction();
  entry->setIsStartAction(true);
  
  // Register set actions
  auto setReady1 = setReg(readyReg, 1, m);
  auto setReady1_2 = setReg(readyReg, 1, m);
  
  auto setDone1 = setReg(doneReg, 1, m);
  auto setDone0 = setReg(doneReg, 0, m);
  auto setReady0 = setReg(readyReg, 0, m);

  auto waitForStart = m->addEmptyInstruction();
  
  entry->then(m->c(1, 1), setReady1, 0);
  entry->then(m->c(1, 1), setDone0, 0);
  entry->then(m->c(1, 1), waitForStart, 1);  
  
  // Program start / end delimiters
  auto progStart = m->addEmpty();
  auto progEnd = m->addEmpty();

  waitForStart->then(notVal(m->ipt("start"), m), waitForStart, 1);
  waitForStart->then(m->ipt("start"), progStart, 0);
  waitForStart->then(m->ipt("start"), setReady0, 0);  
  waitForStart->then(m->ipt("start"), setDone0, 0);  

  auto setReady1ThenWait = m->addEmptyInstruction();
  setReady1ThenWait->then(m->c(1, 1), setDone1, 0);
  //setReady1ThenWait->then(m->c(1, 1), setReady1_2, 0);
  setReady1ThenWait->then(m->c(1, 1), setReady1, 0);  
  setReady1ThenWait->then(m->c(1, 1), waitForStart, 0);

  progStart->then(m->c(1, 1), progEnd, 2);  
  progEnd->then(m->c(1, 1), setReady1ThenWait, 0);
  
  for (Argument& arg : f->args()) {
    Type* tp = arg.getType();
    if (PointerType::classof(tp)) {
      PointerType* ptp = dyn_cast<PointerType>(tp);
      auto utp = ptp->getElementType();
      assert(StructType::classof(utp));
      auto stp = dyn_cast<StructType>(utp);
      cout << "Struct argument name = " << typeString(stp) << endl;
      string str = stp->getName();
      cout << "Name = " << str << endl;

      assert(contains_key(str, builtinModDefs));

      CAC::Module* def = map_find(str, builtinModDefs);
      for (Port pt : def->getInterfacePorts()) {
        if (pt.isInput) {
          m->addOutPort(pt.getWidth(), string(arg.getName()) + "_" + pt.getName());
        } else {
          m->addInPort(pt.getWidth(), string(arg.getName()) + "_" + pt.getName());
        }
        //m->addInstance(getWireMod(c, pt.getWidth()), pt.getName());
      }
      
    } else {
      assert(false);
    }
  }

  // For each basic block we need to create a map from blocks to
  // instruction sets and need to create a map from instructions to
  // invocations?

  // CAC::Module* reg32Mod = c.addModule("reg_32");
  // reg32Mod->setPrimitive(true);
  // reg32Mod->addInPort(1, "en");
  // reg32Mod->addInPort(32, "in");
  // reg32Mod->addOutPort(32, "data");

  // CAC::Module* reg32ModLd = c.addModule("reg_32_ld");
  // CAC::Module* reg32ModSt = c.addModule("reg_32_st");

  // reg32Mod->addAction(reg32ModLd);
  // reg32Mod->addAction(reg32ModSt);

  // map<ReturnInst*, CC*> rets;
  // CC* entryInstr = nullptr;
  
  // for (auto& bb : *f) {
  //   vector<CC*> blkInstrs;
  //   for (auto& instrR : bb) {
  //     Instruction* instr = &instrR;
  //     if (AllocaInst::classof(instr)) {
  //     } else if (BitCastInst::classof(instr)) {
  //       cout << "Ignoring bitcast" << endl;
  //     } else if (CallInst::classof(instr)) {
  //       if (matchesCall("llvm.", instr)) {
  //         cout << "Ignoring llvm builtin " << valueString(instr) << endl;
  //       } else {
  //         string funcName = calledFuncName(instr);          
  //         cout << "Creating code for call to " << funcName << "..." << endl;
  //         CAC::Module* inv = map_find(funcName, builtinModDefs);
  //         assert(inv->isCallingConvention());

  //         auto cc = m->addInvokeInstruction(inv);
  //         blkInstrs.push_back(cc);
          
  //       }
  //     } else if (ReturnInst::classof(instr)) {
  //       auto cc = m->addEmptyInstruction();
  //       blkInstrs.push_back(cc);
  //       rets[dyn_cast<ReturnInst>(instr)] = cc;
  //     } else if (LoadInst::classof(instr)) {
  //       cout << "Need to get module for load" << endl;
  //       auto cc = m->addInvokeInstruction(reg32ModLd);
  //       blkInstrs.push_back(cc);
  //     } else {
  //       cout << "Error: Unsupported instruction " << valueString(instr) << endl;
  //       assert(false);
  //     }

  //   }

  //   for (int i = 0; i < (int) blkInstrs.size() - 1; i++) {
  //     auto instr = blkInstrs[i];
  //     auto nextInstr = blkInstrs[i + 1];
  //     instr->continueTo(m->constOut(1, 1), nextInstr, 1);
  //   }

  //   if (&(f->getEntryBlock()) == &bb) {
  //     cout << "Setting entry instruction" << endl;
      
  //     assert(blkInstrs.size() > 0);
      
  //     entryInstr = blkInstrs[0];

  //     cout << "Done setting instruction" << endl;
  //   }



  // }

  // cout << "Building rv controller" << endl;

  // assert(entryInstr != nullptr);

  // // Create start instruction
  // auto validWire = m->addInstance(getWireMod(c, 1), "valid");

  // auto readyReg = getRegMod(c, 1);
  // auto rdyReg = m->freshInstance(readyReg, "ready_register");

  // auto setReady0 = m->addInvokeInstruction(readyReg->action("reg_1_st"));
  // bindByType(setReady0, rdyReg);
  // setReady0->bind("in", m->constOut(1, 0));
  // setReady0->bind("en", m->constOut(1, 1));  
  
  // auto setReady1 = m->addInvokeInstruction(readyReg->action("reg_1_st"));
  // bindByType(setReady1, rdyReg);  
  // setReady1->setIsStartAction(true);
  // setReady1->bind("in", m->constOut(1, 1));
  // setReady1->bind("en", m->constOut(1, 1));  
  

  // auto readValid = m->addEmptyInstruction();

  // CAC::Module* negMod = getNotMod(c, 1);
  // auto negInst = m->addInstance(negMod, "notValid");
  // auto outWire = m->addInstance(getWireMod(c, 1), "negValidWire");

  // auto negModApply = negMod->action("not_1_apply");
  // auto setNegValid =
  //   m->addInvokeInstruction(negModApply);
  // bindByType(setNegValid, negInst);
  // // setNegValid->bind("not_in", negInst->pt("in"));
  // // setNegValid->bind("not_out", negInst->pt("out"));

  // setNegValid->bind("in", validWire->pt("out"));
  // setNegValid->bind("out", outWire->pt("in"));
  
  // //readValid->continueTo(setNegValid, readValid, 1);
  
  // readValid->continueTo(validWire->pt("out"), entryInstr, 0);
  // readValid->continueTo(validWire->pt("out"), setReady0, 0);  

  // m->addAction(mCall);

  // cout << "Done building module" << endl;
}
