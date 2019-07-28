#include <iostream>
#include "algorithm.h"

using namespace std;
using namespace dbhc;

class Module;
class ModuleInstance;

class Port {
public:
  ModuleInstance* inst;
  std::string portName;
  bool isInput;
  int width;
};

Port getPort(Module* const mod, const std::string& name);

class ModuleInstance {
public:
  Module* source;
  std::string name;

  ModuleInstance(Module* source_, const std::string& name_) :
    source(source_), name(name_) {}

  Port pt(const std::string& name) {
    return getPort(source, name);
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

enum ConnectAndContinueType {
  CONNECT_AND_CONTINUE_TYPE_CONNECT,
  CONNECT_AND_CONTINUE_TYPE_INVOKE
};

class ConnectAndContinue {
public:
  bool isStartAction;
  pair<Port, Port> connection;
  std::vector<Activation> continuations;

  void continueTo(Port condition, CC* next, const int delay) {
    continuations.push_back({condition, next, delay});
  }
};

// How are calling conventions different from
// modules? I want them to always be inlined, but structurally
// how are they different? Maybe they arent? A calling convention
// is just a module with no subconventions, which therefore must
// be inlined in to the code?

typedef Module CallingConvention;

class Module {
  bool isPrimitive;
  std::map<string, Port> primPorts;

  std::set<ModuleInstance*> resources;
  std::set<CC*> body;
  std::set<CallingConvention*> actions;
  std::string name;
  
public:

  Module(const std::string name_) : name(name_), isPrimitive(false) {}

  Port pt(const std::string& name) {
    if (isPrimitive) {
      return map_find(name, primPorts);
    } else {
      assert(false);
    }
  }

  CC* addStartInstruction(const Port a, const Port b) {
    CC* cc = new CC();
    return cc;
  }

  CC* addInstruction(const Port a, const Port b) {
    CC* cc = new CC();
    return cc;
  }
  
  void setPrimitive(const bool isPrim) {
    isPrimitive = isPrim;
  }

  void addInPort(const int width, const std::string& name) {
    assert(isPrimitive);

    assert(!contains_key(name, primPorts));
    primPorts.insert({name, {nullptr, name, true, width}});
  }

  void addOutPort(const int width, const std::string& name) {
    assert(isPrimitive);

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
  }
};

Port getPort(Module* const mod, const std::string& name) {
  return mod->pt(name);
}

class Context {
public:

};

// Example: An adder module has one action, which takes
// an adder as its first argument, and which

// Note: Convert to behavioral code by pushing
// transition conditions across edges in the program
// and adding false transitions to non-transfer blocks

int main() {

  Module* const_1_1 = new Module("const_1_1");
  const_1_1->setPrimitive(true);
  const_1_1->addOutPort(1, "out");
  
  Module* wire16 = new Module("wire16");
  wire16->setPrimitive(true);
  wire16->addInPort(16, "in");
  wire16->addOutPort(16, "out");  
  
  Module* add16 = new Module("add16");
  add16->setPrimitive(true);
  add16->addInPort(16, "in0");
  add16->addInPort(16, "in1");
  add16->addOutPort(16, "out");

  Module* add16Inv = new Module("add16Inv");

  ModuleInstance* oneInst = add16Inv->addInstance(const_1_1, "one");

  ModuleInstance* in0 = add16Inv->addInstance(wire16, "in0");
  ModuleInstance* in1 = add16Inv->addInstance(wire16, "in1");
  ModuleInstance* out = add16Inv->addInstance(wire16, "out");

  ModuleInstance* ain0 = add16Inv->addInstance(wire16, "adder_in0");
  ModuleInstance* ain1 = add16Inv->addInstance(wire16, "adder_in1");
  ModuleInstance* aout = add16Inv->addInstance(wire16, "adder_out");
  
  CC* in0W = add16Inv->addStartInstruction(in0->pt("out"), ain0->pt("in"));
  CC* in1W = add16Inv->addInstruction(in1->pt("out"), ain1->pt("in"));
  CC* outW = add16Inv->addInstruction(out->pt("in"), aout->pt("out"));

  in0W->continueTo(oneInst->pt("out"), in0W, 0);
  in1W->continueTo(oneInst->pt("out"), outW, 0);
  
  add16->addAction(add16Inv);

  // What to do?
  // write a sequential program in this language
  // add hazard descriptions
  // add sensitive port definitions
  // add inlining and context to manage memory
  // implement pipelining

  // For LLVM -> this language we need:
  // structure -> primitive module
  // method -> calling convention
  // calling convention -> hazards
  // data processing instructions -> modules and calling conventions
}
