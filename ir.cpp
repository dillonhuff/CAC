#include "ir.h"

#include <fstream>

using namespace CAC;

namespace CAC {

  int Port::defaultValue() {
    Module* src = inst->source == nullptr ? selfType : inst->source;
    return src->defaultValue(getName());
  }

  Port dest(CC* assigner) {
    assert(assigner->isConnect());
    if (assigner->connection.first.isOutput()) {
      return assigner->connection.second;
    }
    assert(assigner->connection.second.isOutput());

    return assigner->connection.first;
  }
  
  Port source(CC* assigner) {
    assert(assigner->isConnect());
    if (assigner->connection.first.isOutput()) {
      return assigner->connection.first;
    }
    assert(assigner->connection.second.isOutput());

    return assigner->connection.second;
  }
  
  CAC::Module* getWireMod(Context& c, const int width) {
    string name = "wire" + to_string(width);
    if (c.hasModule(name)) {
      return c.getModule(name);
    }

    CAC::Module* w = c.addCombModule(name);
    w->setPrimitive(true);
    w->addInPort(width, "in");
    w->addOutPort(width, "out");
  
    return w;
  }

  
  std::ostream& operator<<(std::ostream& out, const Port& pt) {
    out << (pt.inst == nullptr ? "self." : (pt.inst->getName() + ".")) << pt.getName() << "[" << (pt.isInput ? "in" : "out") << " : " << pt.getWidth() << "]";
    return out;
  }

  void ConnectAndContinue::print(std::ostream& out) const {
    out << (isStartAction ? "on start: " : "") << this << ": do ";
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
  
  Port getOutFacingPort(Module* const mod, const std::string& name) {
    return mod->ept(name);
  }

  Port replacePort(Port pt, map<ModuleInstance*, ModuleInstance*>& resourceMap, map<string, Port>& activeBinding) {
    //cout << "Replacing port " << pt << endl;
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
    //cout << "Inlining " << *invokeInstr << endl;
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

      // cout << "inlining invoked instr = " << *instr << endl;
      // if (instr->isStartAction) {
      //   cout << "Found start of active invocation" << endl;
      // }
      
      CC* iCpy =
        inlineInstrTo(instr, container, resourceMap, invokedBindings);

      ccMap[instr] = iCpy;
    }

    invEnd->continuations = invStart->continuations;
    invStart->continuations = {};
    
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
    } else if (hasPrefix(name, "reg")) {
      return "mod_register #(.WIDTH(16))";
    } else if (hasPrefix(name, "wire")) {
      return "mod_wire #(.WIDTH(16))";
    } else {
      return m->getVerilogDeclString();
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
      if (a.isInput) {
        assert(!b.isInput);
        return verilogString(a, m) + " <= " + verilogString(b, m) + ";";          
      } else {
        assert(b.isInput);
        return verilogString(b, m) + " <= " + verilogString(a, m) + ";";
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
          
          if (c.delay == 0) {
            predConds.push_back(parens(happenedVar(pred, m) +
                                       " && " +
                                       verilogString(c.condition, m)));
          } else {
            predConds.push_back(parens(happenedLastCycleVar(pred, m) +
                                       " && " +
                                       verilogString(c.condition, m)));
            
          }
        }
      }
    }

    return stringList(" || ", predConds);
  }


  vector<Activation> combSuccessors(CC* c) {
    vector<Activation> act;
    for (auto a : c->continuations) {
      if (a.delay == 0) {
        act.push_back(a);
      }
    }
    return act;
  }
  
  set<CC*> nextCombInstructions(CC* c) {
    set<CC*> succ;
    for (auto cont : combSuccessors(c)) {
      succ.insert(cont.destination);
    }
    return succ;
  }

  bool shouldBeWire(Port pt, Module* m) {
    cout << "Checking if " << pt << " should be a wire" << endl;
    if (pt.isInput) {
      cout << "\t" << pt << " is an input" << endl;
      for (auto sc : m->getStructuralConnections()) {
        if ((sc.first == pt) || (sc.second == pt)) {
          return true;
        }
      }

      cout << "\tbut it does not appear in structural connections" << endl;
      return false;
    }

    return true;
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
      //auto pts = r->source->getInterfacePorts();
      auto pts = r->getPorts();

      for (auto pt : pts) {
        if (shouldBeWire(pt, m)) {
          out << "\twire " << "[ " << pt.getWidth() - 1 << " : 0 ] " << verilogString(r->pt(pt.getName()), m) << ";" << endl;
        } else {
          out << "\treg " << "[ " << pt.getWidth() - 1 << " : 0] " << verilogString(r->pt(pt.getName()), m) << ";" << endl;
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

    out << "\t// --- Structural connections" << endl;
    for (auto sc : m->getStructuralConnections()) {
      out << "\tassign " << verilogString(sc.first, m) << " = " << verilogString(sc.second, m) << ";" << endl;
    }
    out << "\t// --- End structural connections" << endl << endl;

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
              if (elem(instr, nextCombInstructions(pred))) {
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

      string defaultString = "";
      if (instr->isConnect() && (dest(instr).isSensitive())) {
        defaultString = verilogString(dest(instr), m) + " <= " + to_string(dest(instr).defaultValue()) + ";\n";
      }
      
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
          out << "\t\t\t\t" << defaultString << endl;
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
        out << "\t\t\t\t" << defaultString << endl;        
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

  Module* getChannelMod(Context& c, const int width) {
    string name = "pipe_channel_" + to_string(width);
    if (c.hasModule(name)) {
      return c.getModule(name);
    }

    auto regMod = c.addCombModule(name);

    regMod->setPrimitive(true);
    regMod->addInPort(width, "in");
    regMod->addOutPort(width, "out");
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
    regMod->setDefaultValue("en", 0);
    CAC::Module* regModLd = c.addModule("reg_" + to_string(width) + "_ld");

    CAC::Module* regModSt = c.addModule("reg_" + to_string(width) + "_st");
    regModSt->addOutPort(width, name + "_in");
    regModSt->addOutPort(1, name + "_en");

    regModSt->addInPort(width, "in");
    regModSt->addInPort(1, "en");

    CC* setEn = regModSt->addStartInstruction(regModSt->ipt("in"),
                                              regModSt->ipt(name + "_in"));

    CC* setData = regModSt->addInstruction(regModSt->ipt("en"),
                                           regModSt->ipt(name + "_en"));
    CC* finish = regModSt->addEmptyInstruction();
    
    setEn->continueTo(regModSt->constOut(1, 1), setData, 0);
    setData->continueTo(regModSt->constOut(1, 1), finish, 1);

      
    regMod->addAction(regModLd);
    regMod->addAction(regModSt);

    return regMod;

  }

  Module* getConstMod(Context& c, const int width, const int value) {
    string name = "const_" + to_string(width) + "_" + to_string(value);
    if (c.hasModule(name)) {
      return c.getModule(name);
    }

    CAC::Module* w = c.addCombModule(name);
    w->setPrimitive(true);
    w->addOutPort(width, "out");
    w->setVerilogDeclString("constant #(.WIDTH(" + to_string(width) + "), .VALUE(" + to_string(value) + "))");
  
    return w;

  }

  std::ostream& operator<<(std::ostream& out, const Module& mod) {
    mod.print(out);
    return out;
  }

  void ConnectAndContinue::bind(const std::string& invokePortName,
                                Port pt) {
    assert(isInvoke());
    assert(invokedMod != nullptr);

    assert(invokedMod->hasPort(invokePortName));
      
    invokeBinding[invokePortName] = pt;
  }

  bool isChannel(Module* m) {
    return hasPrefix(m->getName(), "pipe_channel_");
  }

  // Maybe channel source should actually be a port?
  CC* channelSource(ModuleInstance* chan, Module* container) {
    Port chanIn = chan->pt("in");
    
    for (auto cc : container->getBody()) {
      if (cc->isConnect()) {
        if (cc->connection.first == chanIn) {
          return cc; //cc->connection.second;
        } else if (cc->connection.second == chanIn) {
          return cc; //cc->connection.first;
        }
      }
    }
    cout << "Error: No source for channel " << chan->getName() << endl;
    assert(false);
  }

  void bindByType(CC* invocation, ModuleInstance* toBind) {
    assert(invocation->isInvoke());
    Module* m = toBind->source;
    for (auto pt : toBind->getPorts()) {
      string rcv = m->getName() + "_" + pt.getName();
      if (invocation->invokedMod->hasPort(rcv)) {
        invocation->bind(rcv, pt);
      }
    }
  }

  void replacePort(Port toReplace, Port replacement, CC* instr) {
    if (instr->isEmpty()) {
    } else if (instr->isInvoke()) {
      assert(false);
    } else if (instr->isConnect()) {
      if (instr->connection.first == toReplace) {
        instr->connection.first = replacement;
      }

      if (instr->connection.second == toReplace) {
        instr->connection.second = replacement;
      }
      
    }
  }
  
  void synthesizeChannel(CC* source, ModuleInstance* chan, Module* container) {
    assert(source->isConnect());
    
    // For each path:
    //   for each transition:
    //     create a new register, store to the register
    //     replace use of channel in dest with active wire

    // TODO: Add correct handling for transitions of length one
    Port origPort = source->connection.first == chan->pt("in") ?
      source->connection.second : source->connection.first;
    deque<pair<CC*, Port> > valsAndSources{{source, origPort}};
    set<CC*> visited;
    set<CC*> original;
    for (auto cc : container->getBody()) {
      original.insert(cc);
    }

    while (valsAndSources.size() > 0) {
      pair<CC*, Port> valAndSrc = valsAndSources.front();
      valsAndSources.pop_front();

      CC* val = valAndSrc.first;
      Port src = valAndSrc.second;

      int chanWidth = src.getWidth();
      ModuleInstance* freshReg =
        container->freshInstanceSeq(getRegMod(*(container->getContext()), chanWidth), chan->getName());
      CC* storeRegVal =
        container->addInvokeInstruction(freshReg->source->action(freshReg->source->getName() + "_st"));
      bindByType(storeRegVal, freshReg);
      storeRegVal->bind("in", src);
      storeRegVal->bind("en", container->constOut(1, 1));

      val->continueTo(container->constOut(1, 1), storeRegVal, 0);
      
      Port nextVal = freshReg->pt("data");

      // TODO: Replace connections to chan->pt("out") with nextVal?
      for (auto c : val->continuations) {
        CC* dest = c.destination;
        if (elem(dest, original) && !elem(dest, visited)) {
          if (c.delay == 1) {
            valsAndSources.push_back({dest, nextVal});
            replacePort(chan->pt("out"), nextVal, dest);
          } else {
            assert(c.delay == 0);
            valsAndSources.push_back({dest, src});
            replacePort(chan->pt("out"), src, dest);
          }
        }
      }

      visited.insert(val);
    }

    // Check that there are not references to the channel
    // Delete channel
    container->erase(chan);
  }
  
  void synthesizeChannels(Module* m) {
    for (auto r : m->getResources()) {
      cout << "Resource has type " << r->source->getName() << endl;
      if (isChannel(r->source)) {
        cout << "Need to synthesize channel..." << endl;
        ModuleInstance* chan = r;
        auto source = channelSource(chan, m);
        synthesizeChannel(source, chan, m);
      }
    }

    inlineInvokes(m);
  }

  bool ModuleInstance::hasPt(const std::string& name) const {
    return source->hasPort(name);
  }
    
  
  std::vector<Port> ModuleInstance::getPorts() {
    vector<Port> pts;
    for (auto mp : source->getInterfacePorts()) {
      pts.push_back(this->pt(mp.getName()));
    }
    return pts;
  }

  set<CC*> getAssignmentsToPort(const Port pt, Module* m) {
    assert(pt.isInput);
    
    set<CC*> assigners;
    for (auto cc : m->getBody()) {
      if (cc->wiresUp(pt)) {
        assigners.insert(cc);
      }
    }
    return assigners;
  }

  void reduceStructures(Module* m) {
    // What to do here?
    // - Find all ports that are assigned exactly once
    // - For each port assigned once if it is not a port with a
    //   default value then: Make the connection a no-op and replace
    //   it with a structural connection
    for (auto r : m->getResources()) {
      for (auto pt : r->getPorts()) {
        if (pt.isInput && !pt.isSensitive()) {
          set<CC*> assignments =
            getAssignmentsToPort(pt, m);
          if (assignments.size() == 1) {
            cout << "Found insensitive port " << pt << " that is assigned to in one place" << endl;
            CC* assigner = *begin(assignments);
            Port src = source(assigner);
            assigner->tp = CONNECT_AND_CONTINUE_TYPE_EMPTY;
            m->addSC(pt, src);
          }
        }
      }
    }
  }

  bool Port::isSensitive() const {
    if (inst != nullptr) {
    
      Module* src = inst->source;
      if (contains_key(this->getName(), src->getDefaultValues())) {
        return true;
      }
      return false;
    } else {
      assert(selfType != nullptr);

      Module* src = selfType;
      if (contains_key(this->getName(), src->getDefaultValues())) {
        return true;
      }
      return false;
      
    }
  }
}
