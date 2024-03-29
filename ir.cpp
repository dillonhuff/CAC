#include "ir.h"

#include <fstream>

using namespace CAC;

namespace CAC {

  bool shouldBeWire(Port pt, Module* m) {
    //cout << "Checking if " << pt << " should be a wire" << endl;
    if (pt.isInput) {
      //cout << "\t" << pt << " is an input" << endl;
      for (auto sc : m->getStructuralConnections()) {
        if ((sc.first == pt) || (sc.second == pt)) {
          return true;
        }
      }

      //cout << "\tbut it does not appear in structural connections" << endl;
      return false;
    }

    return true;
  }

  int Port::defaultValue() {
    Module* src = (inst == nullptr || inst->source == nullptr) ?
      selfType : inst->source;
    
    assert(src != nullptr);
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
    w->setVerilogDeclString("mod_wire #(.WIDTH(" + to_string(width) + "))");
  
    return w;
  }

 
 	std::string Port::toString() const {
		return (inst == nullptr ? "self." : (inst->getName() + ".")) + getName() + "[" + (isInput ? "in" : "out") + " : " + to_string(getWidth()) + "]";
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
      if (!contains_key(pt.getName(), activeBinding)) {
        cout << "No port named " << pt.getName() << " in binding:" << endl;
        for (auto b : activeBinding) {
          cout << "\t" << b.first << " -> " << b.second << endl;
        }
      }
      assert(contains_key(pt.getName(), activeBinding));
      
      return map_find(pt.getName(), activeBinding);
    } else {
      return map_find(pt.inst, resourceMap)->pt(pt.getName());
    }
  }

  CC* inlineInstrTo(CC* instr, Module* destMod, map<ModuleInstance*, ModuleInstance*>& resourceMap, map<string, Port>& activeBinding) {
    //cout << "Inlining " << *instr << endl;
    
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
    //cout << "Inlining invoke " << *invokeInstr << endl;
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
        //cout << "Found start of invocation" << endl;
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

  void printVerilog(std::ostream& out, const Port pt, Module* m) {
    int wHigh = pt.getWidth() - 1;
    int wLow = 0;

    //out << (pt.isInput ? "input" : "output reg");
    if (!pt.isOutput()) {
      if (shouldBeWire(pt, m)) {
        out << "output";
      } else {
        out << "output reg";
      }
    } else {
      assert(pt.isOutput());
      out << "input";
    }
    out << " [" << wHigh << " : " << wLow << "] " << pt.getName();
  }

  std::string moduleDecl(Module* m) {
    string name = m->getName();
    if (hasPrefix(name, "add")) {
      return "add #(.WIDTH(16))";
      //} else if (hasPrefix(name, "wire")) {
      // return "mod_wire #(.WIDTH(16))";
    } else {
      return m->getVerilogDeclString();
    }
  }

  bool isConstant(ModuleInstance* inst) {
    return hasPrefix(inst->source->getName(), "const_");
  }

  bool isConstant(Port pt) {
	  if (pt.inst != nullptr) {
	  	return isConstant(pt.inst);
	  }
	  return false;
  }

  std::string drop(std::string pattern, const std::string& name) {
    size_t pos = name.find(pattern);
    if (pos == std::string::npos) {
      return name;
    }

    return name.substr(pos + pattern.size());
  }

  std::string takeUntil(std::string pattern, const std::string& name) {
    size_t pos = name.find(pattern);
    return name.substr(0, pos);
  }
  
  string verilogConstString(ModuleInstance* inst) {
    assert(isConstant(inst));
    string nm = inst->source->getName();
    string rest = drop("const_", nm);
    string width = takeUntil("_", rest);
    reverse(rest);
    string val = takeUntil("_", rest);
    reverse(val);

    return width + "'d" + val;
  }
 string verilogStringLastCycle(const Port pt, Module* m) {
    if (pt.inst != nullptr) {
      ModuleInstance* inst = pt.inst;
      if (isConstant(inst)) {
        return verilogConstString(inst);
        //return pt.inst->getName() + "_" + pt.getName();
      } else {

     return pt.inst->getName() + "_" + pt.getName() + "_last_cycle"; 
	//return pt.inst->getName() + "_" + pt.getName();
      }
    } else {
      return pt.getName();
    }                                                      
  }                                                        
 
 string verilogString(const Port pt, Module* m) {
    if (pt.inst != nullptr) {
      ModuleInstance* inst = pt.inst;
      if (isConstant(inst)) {
        return verilogConstString(inst);
        //return pt.inst->getName() + "_" + pt.getName();
      } else {
        return pt.inst->getName() + "_" + pt.getName();
      }
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
        return verilogString(a, m) + " = " + verilogString(b, m) + ";";          
      } else {
        assert(b.isInput);
        return verilogString(b, m) + " = " + verilogString(a, m) + ";";
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

  string rstPredHappenedString(CC* instr, Module* m, set<CC*> onRst) {
    vector<string> predConds;
    for (auto pred : m->getBody()) {

      if (elem(pred, onRst)) {
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
    }

    return stringList(" || ", predConds);
  }

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
                                       verilogStringLastCycle(c.condition, m)));
            
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

  string assertString(const std::string& cond, const std::string& msg) {
	  return "if (!" + cond + ") begin $display(\"Assertion FAILED: " + cond + ", " + msg + "\"); $finish(1); end";
  }

  string assertString(const std::string& cond) {
  	return "if (!" + cond + ") begin $display(\"Assertion FAILED: " + cond + "\"); $finish(1); end";
  }

  void emitVerilog(Context& c, Module* m) {
    ofstream out(m->getName() + ".v");
    out << "module " << m->getName() << "(" << endl;

    auto pts = m->getInterfacePorts();
    for (int i = 0; i < (int) pts.size(); i++) {
      out << "\t";

      printVerilog(out, reverseDir(pts[i]), m);
      
      if (i < ((int) pts.size()) - 1) {
        out << ", ";
      }


      out << "\n";
    }

    out << "\t);" << endl;

    out << endl;

    out << "\t// --- Start of resource list" << endl << endl;
    
    for (auto r : m->getResources()) {
      if (!isConstant(r)) {
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

    set<Port> usedInDelayedActivation;
    for (auto instr : m->getBody()) {
    	for (auto act : instr->continuations) {
		if (act.delay == 1 && !isConstant(act.condition)) {
			usedInDelayedActivation.insert(act.condition);
		}
	}
    }

	cout << "Used in delayed activations..." << endl;
    for (auto pt : usedInDelayedActivation) {
    	cout << "\t" << pt << endl;

	out << "\treg " << verilogStringLastCycle(pt, m) << ";" << endl;
    }

	for (auto pt : usedInDelayedActivation) {
		out << "\talways @(posedge clk) begin" << endl;
	out << verilogStringLastCycle(pt, m) << " <= " << verilogString(pt, m) << ";" << endl;
		out << "\tend" << endl;	
    out << endl;
	}
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

    map<Port, set<CC*> > setters;
    for (auto instr : m->getBody()) {
      if (instr->isConnect()) {
        Port d = dest(instr);
        if (!contains_key(d, setters)) {
          setters[d] = {};
        }
        setters[d].insert(instr);
      } else {
        assert(instr->isEmpty());
      }
    }

    cout << "All port setters" << endl;
    for (auto entry : setters) {
      cout << entry.first << ": set by..." << endl;
      for (auto instr : entry.second) {
        cout << "\t" << *instr << endl;
      }
    }

     for (auto entry : setters) {
       Port pt = entry.first;
       out << "\t// Controller for port " << pt << endl;
       string defaultStr = "0";
       if (pt.isSensitive()) {
	 defaultStr = to_string(pt.defaultValue());
       }

       vector<pair<string, Port> > resetConds;
       vector<pair<string, Port> > nonResetConds;
       for (auto instr : entry.second) {
	 Port src = source(instr);
	 string predString = predHappenedString(instr, m);
	 if (elem(instr, onRst)) {
	   string rstPredString = rstPredHappenedString(instr, m, onRst);          
	   if (instr->isStartAction) {
	     rstPredString = "1";
	   }
	   resetConds.push_back({rstPredString, src});
	 }
	 nonResetConds.push_back({predString, src});
       }

      out << "\talways @(posedge clk) begin" << endl; 
       for (auto rstCond0 : nonResetConds) {
	       for (auto rstCond1 : nonResetConds) {
		       if (rstCond0.first != rstCond1.first) {
			       string failStr = "Setting port: " + pt.toString() + " from multiple instructions...";
			       out << "\t\t" << assertString(parens("!" + parens(rstCond0.first + " && " + rstCond1.first)), failStr) << ";" << endl;
		       }
	       } 
       
       }
     out << "\tend" << endl;
 
       out << "\talways @(posedge clk) begin" << endl; 
       for (auto rstCond0 : resetConds) {
	       for (auto rstCond1 : resetConds) {
		       if (rstCond0.first != rstCond1.first) {
			       out << "\t\t" << assertString(parens("!" + parens(rstCond0.first + " && " + rstCond1.first))) << ";" << endl;
		       }
	       } 
       
       }
     out << "\tend" << endl;

       out << "\talways @(*) begin" << endl;
       out << "\t\tif (rst) begin" << endl;

       for (auto c : resetConds) {
	 out << "\t\t\tif (" << c.first << ") begin" << endl;
	 out << "\t\t\t\t" << verilogString(pt, m) << " = " << verilogString(c.second, m) << ";" << endl;
	 out << "\t\t\tend else " << endl;
       }
       out << "\t\t\tbegin" << endl;
       out << "\t\t\t\t" << verilogString(pt, m) << " = " << defaultStr << ";" << endl;
       out << "\t\t\tend" << endl;
        
       out << "\t\tend else begin" << endl;

       for (auto c : nonResetConds) {
	 out << "\t\t\tif (" << c.first << ") begin" << endl;
	 out << "\t\t\t\t" << verilogString(pt, m) << " = " << verilogString(c.second, m) << ";" << endl;
	 out << "\t\t\tend else " << endl;
       }
       out << "\t\t\tbegin" << endl;
       out << "\t\t\t\t" << verilogString(pt, m) << " = " << defaultStr << ";" << endl;
       out << "\t\t\tend" << endl;

       out << "\t\tend" << endl;
       out << "\tend" << endl;        
     }
    
    for (auto instr : m->getBody()) {
      
      string predString = predHappenedString(instr, m);
      string body = bodyString(instr, m);

      string defaultString = "";
      if (instr->isConnect() && (dest(instr).isSensitive())) {
        defaultString = verilogString(dest(instr), m) + " = " + to_string(dest(instr).defaultValue()) + ";\n";
      }

      string rstPredString = rstPredHappenedString(instr, m, onRst);
      
      out << "\talways @(*) begin" << endl;
      out << "\t\t// Code for " << *instr << endl;
      out << "\t\tif (rst) begin" << endl;
      if (elem(instr, onRst)) {
        if (instr->isStartAction) {
          //out << "\t\t\t" << body << endl;
          out << "\t\t\t" << happenedVar(instr, m) << " = 1;" << endl;
        } else {

          assert(predString != "");
          out << "\t\t\tif (" << rstPredString << ") begin" << endl;
          //out << "\t\t\t\t" << body << endl;
          out << "\t\t\t\t" << happenedVar(instr, m) << " = 1;" << endl;
          out << "\t\t\tend else begin" << endl;
          //out << "\t\t\t\t" << defaultString << endl;
          out << "\t\t\t\t" << happenedVar(instr, m) << " = 0;" << endl;
          out << "\t\t\tend" << endl;
        }
          
      } else {
        out << "\t\t\t" << happenedVar(instr, m) << " = 0;" << endl;
      }
      out << "\t\tend else begin" << endl;

      if (predString != "") {
        out << "\t\t\tif (" << predString << ") begin" << endl;
        //out << "\t\t\t\t" << body << endl;
        out << "\t\t\t\t" << happenedVar(instr, m) << " = 1;" << endl;;
        out << "\t\t\tend else begin" << endl;
        //out << "\t\t\t\t" << defaultString << endl;        
        out << "\t\t\t\t" << happenedVar(instr, m) << " = 0;" << endl;
        out << "\t\t\tend" << endl;

      } else {
        out << "\t\t\t" << happenedVar(instr, m) << " = 0;" << endl;        
      }

      out << "\t\tend" << endl << endl;      
      out << "\tend" << endl << endl;      
    }

    for (auto instr : m->getBody()) {
      if (instr->continuations.size() > 0) {

        bool anyDelayOne = false;
        for (auto act : instr->continuations) {
          if (act.delay > 0) {
            anyDelayOne = true;
            break;
          }
        }

        if (anyDelayOne) {
          out << "\talways @(posedge clk) begin " << endl;
          out << "\t\t" << happenedLastCycleVar(instr, m) << " <= " << happenedVar(instr, m) << ";" << endl;
          out << "\tend" << endl << endl;
        }
      }
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

    auto regMod = c.addCombModule(name);

    regMod->setPrimitive(true);
    regMod->addInPort(width, "in");
    regMod->addOutPort(width, "out");

    CAC::Module* regModLd = c.addCombModule("not_" + to_string(width) + "_apply");
    regModLd->addOutPort(width, name + "_in");
    regModLd->addInPort(width, name + "_out");    

    regModLd->addInPort(width, "in");
    regModLd->addOutPort(width, "out");
    
    regMod->addAction(regModLd);

    regMod->setVerilogDeclString("notOp #(.WIDTH(" + to_string(width) + "))");

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

    regMod->setVerilogDeclString("mod_register #(.WIDTH(" + to_string(width) + "))");    
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

    if (!invokedMod->hasPort(invokePortName)) {
      cout << "Error: Module " << *invokedMod << endl;
      cout << "has no port " << invokePortName << endl;
    }
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
      //cout << "Replacing port in invoke " << *instr << endl;
      vector<pair<string, Port> > replacements;
      for (auto p : instr->invokedBinding()) {
        if (p.second == toReplace) {
          replacements.push_back({p.first, replacement});
        }
      }

      for (auto r : replacements) {
        instr->bind(r.first, r.second);
      }

    } else if (instr->isConnect()) {
      if (instr->connection.first == toReplace) {
        instr->connection.first = replacement;
      }

      if (instr->connection.second == toReplace) {
        instr->connection.second = replacement;
      }
      
    }

    for (Activation& a : instr->continuations) {
      if (a.condition == toReplace) {
        a.condition = replacement;
      }
    }
  }

  vector<Port> allReferencedPorts(CC* src) {
    vector<Port> all;
    if (src->isConnect()) {
      all.push_back(dest(src));
      all.push_back(source(src));
    } else if (src->isInvoke()) {
      for (auto pt : src->invokedBinding()) {
        all.push_back(pt.second);
      }
    }
    
    for (auto act : src->continuations) {
      all.push_back(act.condition);
    }
    return all;
  }
  
  bool isChannel(ModuleInstance* const mi) {
    //cout << "Checking if " << mi->getName() << " is a channel" << endl;
    bool c = hasPrefix(mi->source->getName(), "pipe_channel_");
    //cout << "\tres = " << c << endl;
    return c;
  }
  
  set<ModuleInstance*> usedChannels(CC* src) {
    vector<Port> pts = allReferencedPorts(src);

    set<ModuleInstance*> usedChannels;
    for (auto port : pts) {
      if (port.inst != nullptr && port.isOutput() && isChannel(port.inst)) {
        usedChannels.insert(port.inst);
      }
    }

    return usedChannels;
  }

  set<ModuleInstance*> definedChannels(CC* src) {
    if (src->isEmpty()) {
      return {};
    } else if (src->isConnect()) {
      Port d = dest(src);
      if (d.inst != nullptr && isChannel(d.inst)) {
        return {d.inst};
      } else {
        return {};
      }
    } else {
      // TODO: Fix?
      return {};
    }
  }
  
  void synthesizeChannel(CC* source, ModuleInstance* chan, Module* container) {
    assert(source->isConnect());

    map<CC*, set<ModuleInstance*> > liveIn;
    map<CC*, set<ModuleInstance*> > liveOut;
    for (auto instr : container->getBody()) {
      liveIn[instr] = {};
      liveOut[instr] = {};
    }

    bool changedVals = true;
    int iter = 0;
    while (changedVals) {
      cout << "Iteration #" << iter << endl;

      changedVals = false;

      auto oldLiveIn = liveIn;
      auto oldLivOut = liveOut;
      
      for (auto instr : container->getBody()) {
        auto liveInP = liveIn[instr];
        auto liveOutP = liveOut[instr];

        set<ModuleInstance*> use = usedChannels(instr);
        //cout << "Used channels at " << *instr << " = " << use.size() << endl;
        
        set<ModuleInstance*> def = definedChannels(instr);
        set<ModuleInstance*> in = use;
        for (auto d : difference(liveOut[instr], def)) {
          in.insert(d);
        }

        liveIn[instr] = in;

        set<ModuleInstance*> newOut;
        for (auto act : instr->continuations) {
          for (auto c : map_find(instr, liveIn)) {
            newOut.insert(c);
          }
        }

        liveOut[instr] = newOut;

        if (liveInP != liveIn[instr]) {
          cout << "Changed values" << endl;
          changedVals = true;
        }

        if (liveOutP != liveOut[instr]) {
          cout << "Changed values" << endl;
          changedVals = true;
        }

      }

      iter++;
    }

    cout << "Stabilized at iteration " << iter << ", Liveness results:" << endl;
    for (auto instr : container->getBody()) {
      auto li = liveIn[instr];
      auto lo = liveOut[instr];
      if (li.size() == 0 && lo.size() == 0) {
        continue;
      }

      cout << "Live in at " << *instr << endl;
      for (auto l : li) {
        cout << "\t" << l->getName() << endl;
      }

      cout << "Live out at " << *instr << endl;
      for (auto l : lo) {
        cout << "\t" << l->getName() << endl;
      }

    }
    
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

    cout << "Synthesizing channel" << endl;
    int i = 0;
    while (valsAndSources.size() > 0) {
      cout << "\tVisits to sources = " << i << endl;
      
      i++;
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
            replacePort(chan->pt("out"), nextVal, dest);
          } else {
            //cout << "Delay for " << *val << " == " << c.delay << endl;
            assert(c.delay == 0);
            replacePort(chan->pt("out"), src, dest);
          }

          // if (c.delay == 0) {
          //   valsAndSources.push_back({dest, src});
          // } else {
          //   if (contains_key(dest, liveIn) &&
          //       elem(chan, map_find(dest, liveIn))) {
          //     valsAndSources.push_back({dest, nextVal});
          //   }
          // }
          if (c.delay == 0) {
            valsAndSources.push_back({dest, src});            
          } else {
            assert(c.delay == 1);
            valsAndSources.push_back({dest, nextVal});            
          }
          
          // if (contains_key(dest, liveOut) &&
          //     elem(chan, map_find(dest, liveOut))) {
            // if (c.delay == 1) {
            //   valsAndSources.push_back({dest, nextVal});
            // } else {
            //   assert(c.delay == 0);
            //   valsAndSources.push_back({dest, src});
            // }
            //}
        }
      }

      visited.insert(val);
    }

    // Check that there are not references to the channel
    // Delete channel
    container->erase(chan);
  }
  
  void synthesizeChannels(Module* m) {
    cout << "Number of instructions when synthesizing channels = " << m->getBody().size() << endl;
    for (auto r : m->getResources()) {
      //cout << "Resource has type " << r->source->getName() << endl;
      if (isChannel(r->source)) {
        //cout << "Need to synthesize channel..." << endl;
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
    

  std::vector<Port> ModuleInstance::getOutPorts() {
    vector<Port> pts;
    for (auto mp : source->getInterfacePorts()) {
      Port p = this->pt(mp.getName());
      if (p.isOutput()) {
        pts.push_back(p);
      }
    }
    return pts;

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
    for (auto r : m->getResources()) {
      for (auto pt : r->getPorts()) {
        if (pt.isInput && !pt.isSensitive()) {
          set<CC*> assignments =
            getAssignmentsToPort(pt, m);
          if (assignments.size() == 1) {
            //cout << "Found insensitive port " << pt << " that is assigned to in one place" << endl;
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

  void synthesizeDelays(Module* m) {
    // Now: For each transition with delays > 1
    bool foundHighDelay = true;
    while (foundHighDelay) {
      foundHighDelay = false;

      for (auto cc : m->getBody()) {
        bool fd = false;        

        for (Activation& ct : cc->continuations) {
          if (ct.delay > 1) {
            // TODO: Add extra instruction
            auto freshD = m->addEmptyInstruction();
            Activation cpy = ct;
            cpy.delay = 1;
            freshD->continuations.push_back(cpy);

            ct.destination = freshD;

            ct.delay--;
            fd = true;
            break;
          }
        }

        if (fd) {
          foundHighDelay = true;
          break;
        }
      }
    }
  }

  Module* ModuleInstance::action(const std::string& actionSuffix) {
    return source->action(source->getName() + "_" + actionSuffix);
  }

  void addBinop(Context& c, const std::string& name, const int cycleLatency) {
    if (c.hasModule(name)) {
      return;
    }
    
    Module* const_1_1 = getConstMod(c, 1, 1);

    Module* add16 = c.addCombModule(name);
    add16->setPrimitive(true);
    add16->addInPort(16, "in0");
    add16->addInPort(16, "in1");
    add16->addOutPort(16, "out");

    assert(!add16->ept("in0").isOutput());  
    assert(!add16->ept("out").isInput);
  
    Module* add16Inv = c.addModule(name + "_apply");
    add16Inv->addInPort(16, "in0");
    add16Inv->addInPort(16, "in1");
    add16Inv->addOutPort(16, "out");    

    add16Inv->addOutPort(16, name + "_in0");
    add16Inv->addOutPort(16, name + "_in1");
    add16Inv->addInPort(16, name + "_out");

    assert(add16Inv->ept(name + "_in0").isOutput());  
    assert(add16Inv->ept(name + "_out").isInput);

    ModuleInstance* oneInst = add16Inv->addInstance(const_1_1, "one");
  
    CC* in0W =
      add16Inv->addStartInstruction(add16Inv->ipt("in0"), add16Inv->ipt(name + "_in0"));
    CC* in1W =
      add16Inv->addInstruction(add16Inv->ipt("in1"), add16Inv->ipt(name + "_in1"));
    CC* outW =
      add16Inv->addInstruction(add16Inv->ipt("out"), add16Inv->ipt(name + "_out"));

    in0W->continueTo(oneInst->pt("out"), in1W, 0);
    in1W->continueTo(oneInst->pt("out"), outW, cycleLatency);

    add16->addAction(add16Inv);
  }

  void deleteNoEffectInstructions(Module* m) {
    set<CC*> noEffect;
    for (auto instr : m->getBody()) {
      if (instr->isEmpty() && instr->continuations.size() == 0) {
        noEffect.insert(instr);
      }
    }

    // 28.5k before, 25.7 after
    for (auto instr : noEffect) {
      m->deleteInstr(instr);
    }

    // Now: Delete instructions with one dest?
    cout << "# of instructions after deleting unused instructions = " << m->getBody().size() << endl;
    int uselessJumps = 0;
    set<CC*> combJumps;
    for (auto instr : m->getBody()) {
      if (instr->isEmpty() && !(instr->isStartAction) && instr->continuations.size() == 1) {
        Activation next = instr->continuations[0];
        if (next.delay == 0) {
          Port cond = next.condition;
          if ((cond.inst != nullptr) && (cond.inst->source->getName() == "const_1_1")) {
            uselessJumps++;
            combJumps.insert(instr);
          }
        }
      }
    }
    cout << "# of comb jump instructions = " << uselessJumps << endl;

    for (auto cj : combJumps) {
      // Find all predecessors
      CC* next = cj->continuations[0].destination;

      // TODO: Actually find predecessors instead of using exhaustive search
      for (auto pred : m->getBody()) {
        pred->replaceJumpsToWith(cj, next);
      }
    }

    for (auto v : combJumps) {
      m->deleteInstr(v);
    }
  }

  bool Module::isDead(ModuleInstance* inst) {
    //cout << "Checking if " << inst->getName() << " is dead" << endl;
    vector<Port> outPts = inst->getOutPorts();
    // for (auto pt : outPts) {
    //   cout << "Output port " << pt << endl;
    // }
    // Filter output ports
    for (auto sc : structuralConnections) {
      if (elem(sc.first, outPts) ||
          elem(sc.second, outPts)) {
        //cout << "Not dead: port referenced in " << sc.first << " <- " << sc.second << endl;
        return false;
      }
    }

    //cout << "Not used in structural connections" << endl;

    for (auto instr : body) {
      if (references(instr, inst)) {
        //cout << *instr << " references " << inst->getName() << ", not dead" << endl;
        return false;
      }
      
      // for (auto pt : outPts) {
      // }

      for (auto& act : instr->continuations) {
        if (elem(act.condition, outPts)) {
          return false;
        }
      }
    }
    return true;
  }
  
  void deleteDeadResources(Module* m) {
    // For each resource:
    //   1. Check if any of its output ports is used by anyone (any sc, any instr
    //      or any activation
    //   2. If not continue
    bool foundDead = true;
    while (foundDead) {
      foundDead = false;

      for (auto r : m->getResources()) {
        if (m->isDead(r)) {
          foundDead = true;
          //cout << "Erasing dead instance " << r->getName() << endl;
          m->erase(r);
        }
      }
    }
  }


  Module* addComparator(Context& c, const std::string& name, const int width) {
    if (c.hasModule(name)) {
      return c.getModule(name);
    }
    
    Module* const_1_1 = getConstMod(c, 1, 1);

    Module* cmpM = c.addCombModule(name);
    cmpM->setPrimitive(true);
    cmpM->addInPort(width, "in0");
    cmpM->addInPort(width, "in1");
    cmpM->addOutPort(1, "out");

    assert(!cmpM->ept("in0").isOutput());  
    assert(!cmpM->ept("out").isInput);
  
    Module* cmpMInv = c.addModule(name + "_apply");
    cmpMInv->addInPort(width, "in0");
    cmpMInv->addInPort(width, "in1");
    cmpMInv->addOutPort(1, "out");    

    cmpMInv->addOutPort(width, name + "_in0");
    cmpMInv->addOutPort(width, name + "_in1");
    cmpMInv->addInPort(1, name + "_out");

    assert(cmpMInv->ept(name + "_in0").isOutput());  
    assert(cmpMInv->ept(name + "_out").isInput);

    ModuleInstance* oneInst = cmpMInv->addInstance(const_1_1, "one");
  
    CC* in0W =
      cmpMInv->addStartInstruction(cmpMInv->ipt("in0"), cmpMInv->ipt(name + "_in0"));
    CC* in1W =
      cmpMInv->addInstruction(cmpMInv->ipt("in1"), cmpMInv->ipt(name + "_in1"));
    CC* outW =
      cmpMInv->addInstruction(cmpMInv->ipt("out"), cmpMInv->ipt(name + "_out"));

    in0W->continueTo(oneInst->pt("out"), in1W, 0);
    in1W->continueTo(oneInst->pt("out"), outW, 0);

    cmpM->addAction(cmpMInv);

    cmpM->setVerilogDeclString(name + " #(.WIDTH(" + to_string(width) + "))");

    return cmpM;

  }
}
