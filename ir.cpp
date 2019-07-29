#include "ir.h"

#include <fstream>

using namespace CAC;

namespace CAC {

  void ConnectAndContinue::print(std::ostream& out) const {
    out << (isStartAction ? "on start: " : "") << this << ": If " << " do ";
    if (isInvoke()) {
      out << "invoke " << invokedMod->getName();
    } else if (isEmpty()) {
      out << "{}";
    } else if (isConnect()) {
      out << "(" << connection.first << ", " << connection.second << ")";
    } else {
      assert(false);
    }
    out << " then continue to [";
    for (auto act : continuations) {
      out << act << " ";
    }
    out << "]";
  }
  
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

  Module* getNotMod(Context& c, const int width) {
    string name = "not_" + to_string(width);
    if (c.hasModule(name)) {
      return c.getModule(name);
    }

    auto regMod = c.addModule(name);

    regMod->setPrimitive(true);
    regMod->addInPort(width, "in");
    regMod->addOutPort(width, "out");

    CAC::Module* regModLd = c.addModule("not_" + to_string(width) + "_apply");
    regMod->addAction(regModLd);


    return regMod;
  }
  
  Module* getRegMod(Context& c, const int width) {
    string name = "reg_" + to_string(width);
    if (c.hasModule(name)) {
      return c.getModule(name);
    }

    auto regMod = c.addModule(name);

    regMod->setPrimitive(true);
    regMod->addInPort(1, "en");
    regMod->addInPort(width, "in");
    regMod->addOutPort(width, "data");

    CAC::Module* regModLd = c.addModule("reg_" + to_string(width) + "_ld");
    CAC::Module* regModSt = c.addModule("reg_" + to_string(width) + "_st");

    regMod->addAction(regModLd);
    regMod->addAction(regModSt);

    return regMod;

  }

  Module* getConstMod(Context& c, const int width, const int value) {
    string name = "const_" + to_string(width) + "_" + to_string(value);
    if (c.hasModule(name)) {
      return c.getModule(name);
    }

    CAC::Module* w = c.addModule(name);
    w->setPrimitive(true);
    w->addInPort(width, "out");
  
    return w;

  }
}
