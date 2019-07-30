#include "ir.h"

#include <fstream>

using namespace CAC;

namespace CAC {

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

  
  std::ostream& operator<<(std::ostream& out, const Port& pt) {
    out << (pt.inst == nullptr ? "self." : (pt.inst->getName() + ".")) << pt.getName() << "[" << pt.getWidth() << "]";
    return out;
  }

  void ConnectAndContinue::print(std::ostream& out) const {
    out << (isStartAction ? "on start: " : "") << this << ": If " << " do ";
    if (isInvoke()) {
      out << "invoke " << invokedMod->getName();
      for (auto pt : invokeBinding) {
        out << "(" << pt.first << ", " << pt.second << ")";
      }
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

  CC* copyInstrTo(CC* instr, Module* destMod) {
    if (instr->isEmpty()) {
      return destMod->addEmptyInstruction();
    } else if (instr->isConnect()) {
      return destMod->addEmptyInstruction();
    } else {
      cout << "Error: Unsupported instruction " << *instr << endl;
      assert(false);
    }
  }
  
  void inlineInvoke(CC* instr, Module* container) {
    cout << "Inlining " << *instr << endl;
    assert(instr->isInvoke());
    
    CC* invStart = instr;

    map<ModuleInstance*, ModuleInstance*> resourceMap;
    
    Module* invoked = instr->invokedModule();

    for (ModuleInstance* inst : invoked->getResources()) {
      Module* instMod = inst->source;
      ModuleInstance* i = container->freshInstance(instMod, inst->getName());
      resourceMap[inst] = i;
    }
    
    map<string, Port> invokedBindings = instr->invokedBinding();
    CC* invEnd = container->addEmptyInstruction();

    auto trueConst =
      container->freshInstance(getConstMod(*(container->getContext()), 1, 1), "true")->pt("out");
    // Inline all instructions connecting dead ones to invEnd
    for (auto instr : invoked->getBody()) {
      CC* iCpy = copyInstrTo(instr, container); //container->addEmptyInstruction();
      if (iCpy->continuations.size() == 0) {
        iCpy->continueTo(trueConst, invEnd, 0);
      }
    }
    
    invStart->tp = CONNECT_AND_CONTINUE_TYPE_EMPTY;
  }
  
  void inlineInvokes(Module* m) {

    bool foundInvoke = true;
    while (foundInvoke) {
      foundInvoke = false;
      for (auto instr : m->getBody()) {
        if (instr->isInvoke()) {
          foundInvoke = true;
          inlineInvoke(instr, m);
        }
      }
    }
  }

  void printVerilog(std::ostream& out, const Port pt) {
    int wHigh = pt.getWidth() - 1;
    int wLow = 0;
    out << (pt.isInput ? "input" : "output") << " [" << wHigh << " : " << wLow << "] " << pt.getName();
  }
  
  void emitVerilog(Context& c, Module* m) {
    ofstream out(m->getName() + ".v");
    out << "module " << m->getName() << "(" << endl;

    auto pts = m->getInterfacePorts();
    for (int i = 0; i < (int) pts.size(); i++) {
      out << "\t";

      printVerilog(out, pts[i]);
      
      if (i < ((int) pts.size()) - 1) {
        out << ", ";
      }


      out << "\n";
    }

    out << "\t);" << endl;

    out << endl;

    out << "\t// --- Start of resource list" << endl << endl;
    
    for (auto r : m->getResources()) {
      out << "\t// Module for " << r->getName() << endl;
    }

    out << endl;

    out << "\t// --- End of resource list" << endl << endl;
    
    for (auto instr : m->getBody()) {
      out << "\talways @(*) begin" << endl;
      out << "\t\t// Code for " << *instr << endl;
      out << "\tend" << endl << endl;
    }

    // For every instruction in the code (no invokes at this point)
    //  Emit an always* for connect
    //  For each distance 0 transition:
    //  Emit a control variable set?

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
    regModLd->addInstance(getWireMod(c, width), "not_in");
    regModLd->addInstance(getWireMod(c, width), "not_in");    
    
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
