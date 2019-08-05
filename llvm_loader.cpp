#include "llvm_loader.h"

#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

using namespace llvm;
using namespace CAC;

Type* getPointedToType(llvm::Type* tp) {
  assert(PointerType::classof(tp));
  return dyn_cast<PointerType>(tp)->getElementType();
}

std::string typeString(Type* const tptr) {
  std::string str;
  llvm::raw_string_ostream ss(str);
  ss << *tptr;

  return ss.str();
}

int getTypeBitWidth(Type* const tp) {
  int width;

  if (IntegerType::classof(tp)) {
    IntegerType* iTp = dyn_cast<IntegerType>(tp);
    width = iTp->getBitWidth();
  } else if (PointerType::classof(tp)) {
    PointerType* pTp = dyn_cast<PointerType>(tp);

    if (!IntegerType::classof(pTp->getElementType())) {
      cout << "Element type = " << typeString(pTp->getElementType()) << endl;
    }
    assert(IntegerType::classof(pTp->getElementType()));

    IntegerType* iTp = dyn_cast<IntegerType>(pTp->getElementType());
    width = iTp->getBitWidth();

  } else if (tp->isFloatTy()) {
    // TODO: Make floating point width parametric
    return 32;
  } else if (tp->isStructTy()) {
    width = 0;
    StructType* stp = dyn_cast<StructType>(tp);
    for (auto* fieldType : stp->elements()) {
      width += getTypeBitWidth(fieldType);
    }
  } else {
    std::cout << "Type = " << typeString(tp) << std::endl;
    assert(ArrayType::classof(tp));
    Type* iTp = dyn_cast<ArrayType>(tp)->getElementType();
    assert(IntegerType::classof(iTp));
    width = dyn_cast<IntegerType>(iTp)->getBitWidth();
          
    //cout << "Array width = " << dyn_cast<ArrayType>(tp)->getElementType() << endl;
    //assert(false);
  }

  return width;

  // assert(IntegerType::classof(tp));

  // return dyn_cast<IntegerType>(tp)->getBitWidth();
}

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

class CodeGenState {
public:
  CAC::Module* m;
  map<AllocaInst*, ModuleInstance*> registersForAllocas;
  map<Value*, ModuleInstance*> channelsForValues;  
  map<Argument*, vector<Port> > portsForArgs;

  ModuleInstance* getChannel(Value* v) {
    if (ConstantInt::classof(v)) {
      int width = getTypeBitWidth(v->getType());
      ConstantInt* vc = dyn_cast<ConstantInt>(v);
      int iVal = vc->getValue().getLimitedValue();
      
      //return m->c(width, iVal);
      CAC::Module* cm = getConstMod(*(m->getContext()), width, iVal);
      return m->freshInstance(cm, "v_const");
    }
    assert(contains_key(v, channelsForValues));
    return map_find(v, channelsForValues);
  }
  ModuleInstance* getReg(Value* targetReg) {
    assert(AllocaInst::classof(targetReg));
    return map_find(dyn_cast<AllocaInst>(targetReg), registersForAllocas);
  }
  
};

// TODO: Add unit test of ready valid controller?
// TODO: Add debug printouts?
void loadLLVMFromFile(Context& c,
                      const std::string& topFunction,
                      const std::string& filePath) {


  
  map<string, CAC::Module*> builtinModDefs;
  CAC::Module* ram32_128 = c.addModule("ram32_128");
  ram32_128->setPrimitive(true);
  ram32_128->addInPort(32, "raddr_0");
  ram32_128->addOutPort(32, "rdata_0");
  ram32_128->addInPort(1, "wen_0");  
  ram32_128->addInPort(32, "waddr_0");
  ram32_128->addInPort(32, "wdata_0");
  ram32_128->setDefaultValue("wen_0", 0);

  CAC::Module* read = c.addModule("ram32_128_read");
  read->addOutPort(32, "ram32_128_raddr_0");
  read->addInPort(32, "ram32_128_rdata_0");  

  read->addInPort(32, "raddr_0");
  read->addOutPort(32, "rdata_0");  
  read->addOutPort(1, "rdata_en_0");  

  CC* setRaddr = read->addStartInstruction(read->ipt("raddr_0"),
                                           read->ipt("ram32_128_raddr_0"));
  CC* readRdata = read->addInstruction(read->ipt("rdata_0"),
                                       read->ipt("ram32_128_rdata_0"));
  setRaddr->continueTo(read->c(1, 1), readRdata, 1);
  
  CAC::Module* write = c.addModule("ram32_128_write");
  // Add set ports, add end and delay

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

  auto entry = m->addEmptyInstruction();
  entry->setIsStartAction(true);

  //trueEntry->then(m->c(1, 1), entry, 1);
  
  // Register set actions
  auto setReady1 = setReg(readyReg, 1, m);
  
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

  // Maybe the problem is that set ready 0 can happen even if we are not in reset?
  // - No that is not the problem
  // Ready one is not happening...
  auto setReady1ThenWait = m->addEmptyInstruction();
  setReady1ThenWait->then(m->c(1, 1), setDone1, 0);
  setReady1ThenWait->then(m->c(1, 1), setReady1, 0);  
  setReady1ThenWait->then(m->c(1, 1), waitForStart, 0);

  progStart->then(m->c(1, 1), progEnd, 2);  
  progEnd->then(m->c(1, 1), setReady1ThenWait, 0);

  CodeGenState state;
  state.m = m;
  
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
      vector<Port> portsForArg;
      for (Port pt : def->getInterfacePorts()) {
        string ptName = string(arg.getName()) + "_" + pt.getName();
        if (pt.isInput) {
          m->addOutPort(pt.getWidth(), ptName);
        } else {
          m->addInPort(pt.getWidth(), ptName);
        }
        portsForArg.push_back(m->ipt(ptName));
      }
      state.portsForArgs[&arg] = portsForArg;
      
    } else {
      assert(false);
    }
  }

  for (auto& bb : *f) {
    for (auto& instrR : bb) {
      auto instr = &instrR;
      if (AllocaInst::classof(instr)) {
        int width = getTypeBitWidth(getPointedToType(instr->getType()));
        auto chan = m->freshInstance(getRegMod(c, width), "alloca");
        state.registersForAllocas[dyn_cast<AllocaInst>(instr)] = chan;
      } else if (LoadInst::classof(instr)) {
        int width = getTypeBitWidth(instr->getType());
        auto chan = m->freshInstance(getRegMod(c, width), "channel");     
        state.channelsForValues[dyn_cast<Value>(instr)] = chan;
      }
    }
  }

  CC* entryInstr = nullptr;

  for (auto& bb : *f) {
    vector<CC*> blkInstrs;
    for (auto& instrR : bb) {
      Instruction* instr = &instrR;
      if (AllocaInst::classof(instr)) {
      } else if (BitCastInst::classof(instr)) {
        cout << "Ignoring bitcast" << endl;
      } else if (CallInst::classof(instr)) {
        if (matchesCall("llvm.", instr)) {
          cout << "Ignoring llvm builtin " << valueString(instr) << endl;
        } else {
          string funcName = calledFuncName(instr);          
          cout << "Creating code for call to " << funcName << "..." << endl;

          CAC::Module* inv = map_find(funcName, builtinModDefs);
          assert(inv->isCallingConvention());

          auto cc = m->addInvokeInstruction(inv);

          if (funcName == "read") {
            // TODO: Generalize for arbitrary argument
            cc->bind("ram32_128_raddr_0", m->ipt("ram_raddr_0"));
            cc->bind("ram32_128_rdata_0", m->ipt("ram_rdata_0"));

            Value* addr = instr->getOperand(1);
            auto addrChannel = state.getChannel(addr);
            Value* targetVal = instr->getOperand(2);
            auto targetReg = state.getReg(targetVal);

            cc->bind("raddr_0", addrChannel->pt("out"));
            cc->bind("rdata_0", targetReg->pt("in"));
            cc->bind("rdata_en_0", targetReg->pt("en"));                        
          }

          blkInstrs.push_back(cc);
        }
      } else if (ReturnInst::classof(instr)) {
        auto cc = m->addEmptyInstruction();
        cc->then(m->c(1, 1), progEnd, 0);
        blkInstrs.push_back(cc);
      } else if (LoadInst::classof(instr)) {
        cout << "Need to get module for load" << endl;
        auto arg = instr->getOperand(0);
        assert(AllocaInst::classof(arg));
        ModuleInstance* reg = map_find(dyn_cast<AllocaInst>(arg), state.registersForAllocas);
        ModuleInstance* chan = map_find(dyn_cast<Value>(instr),
                                        state.channelsForValues);
        CC* readReg = m->addCC(chan->pt("in"), reg->pt("data"));
        
        blkInstrs.push_back(readReg);

      } else {
        cout << "Error: Unsupported instruction " << valueString(instr) << endl;
        assert(false);
      }

    }

    for (int i = 0; i < (int) blkInstrs.size() - 1; i++) {
      auto instr = blkInstrs[i];
      auto nextInstr = blkInstrs[i + 1];
      instr->continueTo(m->constOut(1, 1), nextInstr, 1);
    }

    if (&(f->getEntryBlock()) == &bb) {
      cout << "Setting entry instruction" << endl;
      
      assert(blkInstrs.size() > 0);
      
      entryInstr = blkInstrs[0];
      progStart->then(m->c(1, 1), entryInstr, 0);

      cout << "Done setting instruction" << endl;
    }
  }

}
