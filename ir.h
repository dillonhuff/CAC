#pragma once

#include <iostream>
#include "algorithm.h"

using namespace std;
using namespace dbhc;

namespace CAC {

  class Context;
  class Module;

  void print(std::ostream& out, Module* source);  

  class ModuleInstance;

  class Port {
  public:
    ModuleInstance* inst;
    std::string portName;
    bool isInput;
    int width;

    int getWidth() const {
      return width;
    }

    std::string getName() const {
      return portName;
    }
  };

  static inline
  Port reverseDir(const Port pt) {
    Port cpy = pt;
    cpy.isInput = !pt.isInput;
    return cpy;
  }

  static inline
  bool operator==(const Port& a, const Port& b) {
    return (a.inst == b.inst) && (a.getName() == b.getName());
  }

  std::ostream& operator<<(std::ostream& out, const Port& pt);
  
  Port getPort(Module* const mod, const std::string& name);

  class ModuleInstance {
  public:
    Module* source;
    std::string name;

    ModuleInstance(Module* source_, const std::string& name_) :
      source(source_), name(name_) {}

    std::string getName() const { return name; }

    Port pt(const std::string& name) {
      Port pt = getPort(source, name);
      pt.inst = this;
      pt = reverseDir(pt);
      return pt;
    }

    void print(std::ostream& out) {
      out << "submodule " << name << " of type " << endl;
      CAC::print(out, source);
    }
  };

  class ConnectAndContinue;

  typedef ConnectAndContinue CC;

  class Activation {
  public:
    Port condition;
    CC* destination;
    int delay;
  };

  static inline
  std::ostream& operator<<(std::ostream& out, const Activation& act) {
    out << "(" << act.condition << ", " << act.destination << ", " << act.delay << ")";
    return out;
  }

  enum ConnectAndContinueType {
    CONNECT_AND_CONTINUE_TYPE_CONNECT,
    CONNECT_AND_CONTINUE_TYPE_INVOKE,
    CONNECT_AND_CONTINUE_TYPE_EMPTY    
  };

  class ConnectAndContinue {
  public:
    ConnectAndContinueType tp;
    bool isStartAction;
    pair<Port, Port> connection;
    std::vector<Activation> continuations;

    Module* invokedMod;
    std::map<std::string, Port> invokeBinding;

    Module* invokedModule() const { assert(isInvoke()); return invokedMod; }

    map<string, Port> invokedBinding() const {
      assert(isInvoke());
      return invokeBinding;
    }
    
    void bind(const std::string& invokePortName,
              Port pt) {
      assert(isInvoke());
      invokeBinding[invokePortName] = pt;
    }

    bool wiresUp(const Port pt) {
      if (isEmpty()) {
        return false;
      }

      if (isInvoke()) {
        return false;
      }

      if (isConnect()) {
        return (connection.first == pt) || (connection.second == pt);
      }

      return false;
    }
    
    void setIsStartAction(const bool isStart) {
      isStartAction = isStart;
    }

    void continueTo(Port condition, CC* next, const int delay) {
      continuations.push_back({condition, next, delay});
    }

    bool isInvoke() const {
      return tp == CONNECT_AND_CONTINUE_TYPE_INVOKE;
    }

    bool isEmpty() const {
      return tp == CONNECT_AND_CONTINUE_TYPE_EMPTY;
    }

    bool isConnect() const {
      return tp == CONNECT_AND_CONTINUE_TYPE_CONNECT;
    }

    void print(std::ostream& out) const;    
  };

  static inline
  std::ostream& operator<<(std::ostream& out, const ConnectAndContinue& instr) {
    instr.print(out);
    return out;
  }

  typedef Module CallingConvention;

  Module* getConstMod(Context& c, const int width, const int value);
  Module* getRegMod(Context& c, const int width);
  Module* getNotMod(Context& c, const int width);
  
  class Module {
    bool isPrimitive;
    std::map<string, Port> primPorts;

    std::set<ModuleInstance*> resources;
    std::set<CC*> body;
    std::map<std::string, CallingConvention*> actions;
    std::string name;

    int uniqueNum;

    Context* context;
  
  public:

    Module(const std::string name_) : isPrimitive(false), name(name_), uniqueNum(0) {}

    std::set<CC*> getBody() const { return body; }
    std::set<ModuleInstance*> getResources() const { return resources; }    

    CallingConvention* action(const std::string& name) {
      assert(contains_key(name, actions));
      return map_find(name, actions);
    }

    bool isCallingConvention() const {
      return !isPrimitive && actions.size() == 0;
    }
    
    void setContext(Context* ctx) {
      context = ctx;
    }

    ModuleInstance* freshInstance(Module* tp, const std::string& name) {
      string fullName = name + "_" + to_string(uniqueNum);
      uniqueNum++;
      return addInstance(tp, fullName);
    }

    Context* getContext() {
      return context;
    }

    Port constOut(const int width, const int value) {
      assert(context != nullptr);
      Module* constInt = getConstMod(*context, width, value);
      ModuleInstance* c = freshInstance(constInt, "const");
      return c->pt("out");
    }

    vector<Port> allPorts() const {
      if (isPrimitive) {
        return getInterfacePorts();
      }
      
      vector<Port> allpts;
      for (auto m : resources) {
        auto rPorts = m->source->getInterfacePorts();
        for (auto rpt : rPorts) {
          allpts.push_back(m->pt(rpt.getName()));
        }

      }
      cout << "Total # of ports = " << allpts.size() << endl;
      return allpts;
    }

    bool neverWiredUp(const Port pt) const {
      for (auto instr : body) {
        if (instr->wiresUp(pt)) {
          return false;
        }
      }
      return true;
    }

    vector<Port> getInterfacePorts() const {
      

        vector<Port> pts;
        for (auto pt : primPorts) {
          pts.push_back(pt.second);
        }
        return pts;
      // } else {
      //   vector<Port> pts = allPorts();
      //   vector<Port> iPorts;
      //   for (auto pt : pts) {
      //     if (neverWiredUp(pt)) {
      //       iPorts.push_back(pt);
      //     }
      //   }
      //   return iPorts;
      // }
    }
    
    Port pt(const std::string& name) {
      return map_find(name, primPorts);      
    }

    CC* addInvokeInstruction(CallingConvention* call) {
      assert(call->isCallingConvention());
      CC* cc = new CC();
      cc->invokedMod = call;
      cc->tp = CONNECT_AND_CONTINUE_TYPE_INVOKE;
      body.insert(cc);
      return cc;
    }
    
    CC* addEmptyInstruction() {
      CC* cc = new CC();
      cc->tp = CONNECT_AND_CONTINUE_TYPE_EMPTY;
      body.insert(cc);
      return cc;
    }

    CC* addStartInstruction(const Port a, const Port b) {
      CC* cc = new CC();
      cc->tp = CONNECT_AND_CONTINUE_TYPE_CONNECT;
      cc->connection.first = a;
      cc->connection.second = b;      
      body.insert(cc);      
      return cc;
    }

    CC* addInstruction(const Port a, const Port b) {
      CC* cc = new CC();
      cc->tp = CONNECT_AND_CONTINUE_TYPE_CONNECT;
      cc->connection.first = a;
      cc->connection.second = b;
      body.insert(cc);      
      return cc;
    }
  
    void setPrimitive(const bool isPrim) {
      isPrimitive = isPrim;
    }

    void addInPort(const int width, const std::string& name) {
      assert(!contains_key(name, primPorts));

      primPorts.insert({name, {nullptr, name, true, width}});
    }

    void addOutPort(const int width, const std::string& name) {
      assert(!contains_key(name, primPorts));
      primPorts.insert({name, {nullptr, name, false, width}});

    }
  
    int numActions() {
      return actions.size();
    }

    ModuleInstance* addInstance(Module* tp, const std::string& name) {
      auto i = new ModuleInstance(tp, name);
      resources.insert(i);
      return i;
    }
  
    void addAction(CallingConvention* callingC) {
      assert(callingC->numActions() == 0);
      actions.insert({callingC->getName(), callingC});
    }

    void print(std::ostream& out) const {
      out << "module " << name << endl << endl;

      vector<Port> pts = getInterfacePorts();
      out << pts.size() << " ports..." << endl;
      for (auto pt : pts) {
        cout << "\t" << pt << endl;
      }
      out << endl << endl;

      out << actions.size() << " actions..." << endl;
      for (auto action : actions) {
        action.second->print(out);
        out << endl << endl;
      }
      out << "end of actions for " << name << endl << endl;

      out << resources.size() << " submodules..." << endl;
      for (ModuleInstance* mod : resources) {
        mod->print(out);
        out << endl << endl;
      }

      out << "End of submodules" << endl << endl;

      out << "Body:" << endl;
      for (auto instr : body) {
        out << *instr << endl;
      }

      out << endl;
      
      out << "endmodule "<< name << endl;
    }

    std::string getName() const { return name; }
  };

  static inline
  std::ostream& operator<<(std::ostream& out, const Module& mod) {
    mod.print(out);
    return out;
  }

  class Context {
    std::map<std::string, Module*> mods;
    
  public:

    Module* getModule(const std::string& name) {
      return map_find(name, mods);
    }

    bool hasModule(const std::string& name) {
      return contains_key(name, mods);
    }
    
    Module* addModule(const std::string& name) {
      if (contains_key(name, mods)) {
        cout << "Error: Module already contains " << name << endl;
      }
      assert(!contains_key(name, mods));
      
      mods[name] = new Module(name);
      mods[name]->setContext(this);
      mods[name]->addInPort(1, "clk");
      mods[name]->addInPort(1, "rst");      

      return map_find(name, mods);
    }
    
  };

  void emitVerilog(Context& c, Module* m);

  CAC::Module* getWireMod(Context& c, const int width);

  void inlineInvokes(Module* m);
}
