#include "llvm_loader.h"

#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Module.h>

using namespace llvm;
using namespace CAC;

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

void loadLLVMFromFile(Context& c,
                      const std::string& topFunction,
                      const std::string& filePath) {
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

  m->addAction(mCall);
  
  // Now: For each argument add wires to the API
  // Add ram primitive to context
  // Create ram invoke
}
