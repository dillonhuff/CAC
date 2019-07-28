#include "ir.h"

#include <fstream>

using namespace CAC;

namespace CAC {

  Port getPort(Module* const mod, const std::string& name) {
    return mod->pt(name);
  }


  void emitVerilog(Context& c, Module* m) {
    ofstream out(m->getName() + ".v");
    out << "module " << m->getName() << "();" << endl;
    // For every instruction in the code (no invokes at this point)
    //  Emit an always block?
    out << "endmodule";
    out.close();
  }

  void print(std::ostream& out, Module* source) {
    source->print(out);
  }
}
