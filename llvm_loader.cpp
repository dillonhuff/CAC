#include "llvm_loader.h"

#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

using namespace llvm;
using namespace CAC;

static inline
bool hasPrefix(const std::string str, const std::string prefix) {
  auto res = std::mismatch(prefix.begin(), prefix.end(), str.begin());

  if (res.first == prefix.end()) {
    return true;
  }

  return false;
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

CAC::Module* getWireMod(Context& c, const int width) {
  string name = "wire" + to_string(width);
  if (c.hasModule(name)) {
    return c.getModule(name);
  }

  CAC::Module* w = c.addModule(name);
  w->setPrimitive(true);
  w->addInPort(width, "in");
  w->addInPort(width, "out");
  
  return w;
}

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

  CAC::Module* read = c.addModule("ram32_128_read");
  auto ramRaddr = read->addInstance(getWireMod(c, 32), "ram_raddr");
  auto ramData = read->addInstance(getWireMod(c, 32), "ram_rdata");  

  auto resRaddr = read->addInstance(getWireMod(c, 32), "res_raddr");
  auto resRdata = read->addInstance(getWireMod(c, 32), "res_rdata");

  CC* setRaddr = read->addStartInstruction(ramRaddr->pt("in"),
                                           resRaddr->pt("out"));
  CC* readRdata = read->addInstruction(ramData->pt("in"),
                                       resRdata->pt("in"));
  setRaddr->continueTo(read->constOut(1, 1), readRdata, 1);
  
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
        m->addInstance(getWireMod(c, pt.getWidth()), pt.getName());
      }
      
    } else {
      assert(false);
    }
  }

  // For each basic block we need to create a map from blocks to
  // instruction sets and need to create a map from instructions to
  // invocations?

  CAC::Module* reg32Mod = c.addModule("reg_32");
  reg32Mod->setPrimitive(true);
  reg32Mod->addInPort(1, "en");
  reg32Mod->addInPort(32, "in");
  reg32Mod->addOutPort(32, "data");

  CAC::Module* reg32ModLd = c.addModule("reg_32_ld");
  CAC::Module* reg32ModSt = c.addModule("reg_32_st");

  reg32Mod->addAction(reg32ModLd);
  reg32Mod->addAction(reg32ModSt);

  map<ReturnInst*, CC*> rets;
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
          blkInstrs.push_back(cc);
          
        }
      } else if (ReturnInst::classof(instr)) {
        auto cc = m->addEmptyInstruction();
        blkInstrs.push_back(cc);
        rets[dyn_cast<ReturnInst>(instr)] = cc;
      } else if (LoadInst::classof(instr)) {
        cout << "Need to get module for load" << endl;
        auto cc = m->addInvokeInstruction(reg32ModLd);
        blkInstrs.push_back(cc);
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

      cout << "Done setting instruction" << endl;
    }



  }

  cout << "Building rv controller" << endl;

  assert(entryInstr != nullptr);

  // Create start instruction
  auto validWire = m->addInstance(getWireMod(c, 1), "valid");

  auto readyReg = getRegMod(c, 1);

  auto setReady0 = m->addInvokeInstruction(readyReg->action("reg_1_st"));
  
  auto setReady1 = m->addInvokeInstruction(readyReg->action("reg_1_st"));
  setReady1->setIsStartAction(true);

  auto readValid = m->addEmptyInstruction();

  CAC::Module* negMod = getNotMod(c, 1);
  auto negInst = m->addInstance(negMod, "notValid");
  auto outWire = m->addInstance(getWireMod(c, 1), "negValidWire");

  auto negModApply = negMod->action("not_1_apply");
  auto setNegValid =
    m->addInvokeInstruction(negModApply);
  setNegValid->bind("not_in", negInst->pt("in"));
  setNegValid->bind("not_out", negInst->pt("out"));

  setNegValid->bind("data_in", validWire->pt("out"));
  setNegValid->bind("data_out", outWire->pt("in"));
  
  //readValid->continueTo(setNegValid, readValid, 1);
  
  readValid->continueTo(validWire->pt("out"), entryInstr, 0);
  readValid->continueTo(validWire->pt("out"), setReady0, 0);  

  m->addAction(mCall);

  cout << "Done building module" << endl;
}
