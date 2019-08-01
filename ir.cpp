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

  Port replacePort(Port pt, map<ModuleInstance*, ModuleInstance*>& resourceMap, map<string, Port>& activeBinding) {
    if (pt.inst == nullptr) {
      return map_find(pt.getName(), activeBinding);
    } else {
      return map_find(pt.inst, resourceMap)->pt(pt.getName());
    }
  }

  CC* inlineInstrTo(CC* instr, Module* destMod, map<ModuleInstance*, ModuleInstance*>& resourceMap, map<string, Port>& activeBinding) {
    CC* cpy = nullptr;
    if (instr->isEmpty()) {
      cpy = destMod->addEmptyInstruction();
    } else if (instr->isConnect()) {
      cpy =
        destMod->addInstruction(replacePort(instr->connection.first,
                                            resourceMap,
                                            activeBinding),
                                replacePort(instr->connection.second,
                                            resourceMap,
                                            activeBinding));
    } else {
      cout << "Error: Unsupported instruction " << *instr << endl;
      assert(false);
    }

    // Invoked instructions are never starts
    cpy->setIsStartAction(false);
    
    return cpy;
  }
  
  void inlineInvoke(CC* invokeInstr, Module* container) {
    cout << "Inlining " << *invokeInstr << endl;
    assert(invokeInstr->isInvoke());
    
    CC* invStart = invokeInstr;

    map<ModuleInstance*, ModuleInstance*> resourceMap;
    
    Module* invoked = invokeInstr->invokedModule();

    for (ModuleInstance* inst : invoked->getResources()) {
      Module* instMod = inst->source;
      ModuleInstance* i = container->freshInstance(instMod, inst->getName());
      resourceMap[inst] = i;
    }
    
    map<string, Port> invokedBindings = invokeInstr->invokedBinding();
    CC* invEnd = container->addEmptyInstruction();

    auto trueConst =
      container->freshInstance(getConstMod(*(container->getContext()), 1, 1), "true")->pt("out");
    // Inline all instructions connecting dead ones to invEnd
    map<CC*, CC*> ccMap;
    for (auto instr : invoked->getBody()) {

      cout << "inlining invoked instr = " << *instr << endl;
      if (instr->isStartAction) {
        cout << "Found start of active invocation" << endl;
      }
      
      CC* iCpy =
        inlineInstrTo(instr, container, resourceMap, invokedBindings);

      ccMap[instr] = iCpy;
    }

    for (auto ccPair : ccMap) {
      CC* instr = ccPair.first;
      CC* cpy = ccPair.second;
      vector<Activation> newActivations;
      for (auto act : instr->continuations) {
        Port newCond = replacePort(act.condition, resourceMap, invokedBindings);
        CC* newDest = map_find(act.destination, ccMap);
        int newDelay = act.delay;
        newActivations.push_back({newCond, newDest, newDelay});
      }
      cpy->continuations = newActivations;

      if (cpy->continuations.size() == 0) {
        cpy->continueTo(trueConst, invEnd, 0);
      }
      
      if (instr->isStartAction) {
        cout << "Found start of invocation" << endl;
        invStart->continuations.push_back({trueConst, map_find(instr, ccMap), 0});
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
    out << (pt.isInput ? "input" : "output reg") << " [" << wHigh << " : " << wLow << "] " << pt.getName();
  }

  std::string moduleDecl(Module* m) {
    string name = m->getName();
    if (hasPrefix(name, "add")) {
      return "add #(.WIDTH(16))";
    } else {
      return "constant #(.WIDTH(1), .VALUE(1))";
    }
  }

  string verilogString(const Port pt, Module* m) {
    if (pt.inst != nullptr) {
      return pt.inst->getName() + "_" + pt.getName();
    } else {
      return pt.getName();
    }
  }
  
  string bodyString(CC* instr, Module* m) {
    if (instr->isEmpty()) {
      return "";
    } else {
      assert(instr->isConnect());
      Port a = instr->connection.first;
      Port b = instr->connection.second;
      // TODO: Order the ports
      if (a.isInput) {
        assert(!b.isInput);
        if (a.inst == nullptr) {
          return verilogString(b, m) + " <= " + verilogString(a, m) + ";";          
        } else {
          return verilogString(a, m) + " <= " + verilogString(b, m) + ";";
        }
      } else {
        assert(b.isInput);
        if (a.inst == nullptr) {
          return verilogString(a, m) + " <= " + verilogString(b, m) + ";";
        } else {
          return verilogString(b, m) + " <= " + verilogString(a, m) + ";";          
        }
      }
    }
  }

  string happenedVar(CC* instr, Module* m) {
    return "i_" + to_string((uint64_t) instr) + "_happened";
  }
  
  string happenedLastCycleVar(CC* instr, Module* m) {
    return "i_" + to_string((uint64_t) instr) + "_happened_last_cycle";
  }

  string stringList(const std::string& sep, const std::vector<string>& strs) {
    string res = "";
    if (strs.size() == 0) {
      return res;
    }

    for (int i = 0; i < (int) strs.size(); i++) {
      res += strs[i];
      if (i < ((int) strs.size()) - 1) {
        res += sep;
      }
    }

    return res;
  }

  string parens(const string& str) { return "(" + str + ")"; }
  
  string predHappenedString(CC* instr, Module* m) {
    vector<string> predConds;
    for (auto pred : m->getBody()) {
      for (auto c : pred->continuations) {
        if (c.destination == instr) {
          assert(0 <= c.delay && c.delay <= 1);
          
          // TODO: Check transition distance
          predConds.push_back(parens(happenedVar(pred, m) +
                                     " && " +
                                     verilogString(c.condition, m)));
        }
      }
    }

    return stringList(" || ", predConds);
  }

  set<CC*> successors(CC* c) {
    set<CC*> succ;
    for (auto cont : c->continuations) {
      succ.insert(cont.destination);
    }
    return succ;
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
      auto pts = r->source->getInterfacePorts();

      for (auto pt : pts) {
        if (pt.isInput) {
          out << "\treg " << "[ " << pt.getWidth() - 1 << " : 0 ] " << verilogString(r->pt(pt.getName()), m) << ";" << endl;
        } else {
          out << "\twire " << "[ " << pt.getWidth() - 1 << " : 0] " << verilogString(r->pt(pt.getName()), m) << ";" << endl;
        }
      }
      out << "\t" << moduleDecl(r->source) + " " + r->getName() + "(";

      for (int i = 0; i < (int) pts.size(); i++) {
        out << "." << pts[i].getName() << "(" << verilogString(r->pt(pts[i].getName()), m) << ")";
        if (i < ((int) pts.size() - 1)) {
          out << ", ";
        }
      }
      
      out << ");" << endl << endl;
    }

    out << endl;

    out << "\t// --- End of resource list" << endl << endl;

    // Emit check to see if any predecessor happened?
    // Also: Emit predecessor variables

    for (auto instr : m->getBody()) {
      out << "\treg " << happenedVar(instr, m) << ";" << endl;
      out << "\treg " << happenedLastCycleVar(instr, m) << ";" << endl;      
    }

    out << endl;

    set<CC*> work;
    set<CC*> onRst;
    bool found = true;
    while (found) {
      found = false;

      for (auto instr : m->getBody()) {
        if (!elem(instr, onRst)) {
          if (instr->isStartAction) {
            onRst.insert(instr);
            found = true;
          } else {
            for (auto pred : onRst) {
              if (elem(instr, successors(pred))) {
                onRst.insert(instr);
                found = true;
                break;
              }
            }
          }
        }
      }

    }
    
    for (auto instr : m->getBody()) {
      string predString = predHappenedString(instr, m);
      string body = bodyString(instr, m);
      out << "\talways @(*) begin" << endl;
      out << "\t\t// Code for " << *instr << endl;
      out << "\t\tif (rst) begin" << endl;
      if (elem(instr, onRst)) {
        if (instr->isStartAction) {
          out << "\t\t\t" << body << endl;
          out << "\t\t\t" << happenedVar(instr, m) << " <= 1;" << endl;
        } else {

          assert(predString != "");
          out << "\t\t\tif (" << predString << ") begin" << endl;
          out << "\t\t\t\t" << body << endl;
          out << "\t\t\t\t" << happenedVar(instr, m) << " <= 1;" << endl; 
          out << "\t\t\tend else begin" << endl;
          out << "\t\t\t\t" << happenedVar(instr, m) << " <= 0;" << endl;
          out << "\t\t\tend" << endl;

        }
          
      } else {
        out << "\t\t\t" << happenedVar(instr, m) << " <= 0;" << endl;
      }
      out << "\t\tend else begin" << endl;

      if (predString != "") {
        out << "\t\t\tif (" << predString << ") begin" << endl;
        out << "\t\t\t\t" << body << endl;
        out << "\t\t\t\t" << happenedVar(instr, m) << " <= 1;" << endl; 
        out << "\t\t\tend else begin" << endl;
        out << "\t\t\t\t" << happenedVar(instr, m) << " <= 0;" << endl;
        out << "\t\t\tend" << endl;

      }

      out << "\t\tend" << endl << endl;      
      out << "\tend" << endl << endl;      
    }

    for (auto instr : m->getBody()) {
      out << "\talways @(posedge clk) begin " << endl;
      out << "\t\t" << happenedLastCycleVar(instr, m) << " <= " << happenedVar(instr, m) << ";" << endl;
      out << "\tend" << endl << endl;
    }

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
