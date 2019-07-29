#include "llvm_loader.h"

#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

using namespace llvm;
using namespace CAC;

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
  for (auto& bb : *f) {
    vector<CC*> blkInstrs;
    for (auto& instrR : bb) {
      Instruction* instr = &instrR;
      if (AllocaInst::classof(instr)) {
      } else if (ReturnInst::classof(instr)) {
        auto cc = m->addEmptyInstruction();
        blkInstrs.push_back(cc);
      } else {
        auto cc = m->addEmptyInstruction();
        blkInstrs.push_back(cc);
      }
    }

  }

  // Call: set valid and wait for ready
  // and simultaneously set raddr, rdata, waddr, wdata to wires
  // while done is not high

  // Im also held up by how wires in arguments will map to llvm values
  // during translation
  m->addAction(mCall);
  
  // Now: For each argument add wires to the API
  // Add ram primitive to context
  // Create ram invoke
}
