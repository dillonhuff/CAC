#include <iostream>
#include "algorithm.h"

using namespace std;
using namespace dbhc;

class Module;
  
class ModuleInstance {
public:
  Module* source;
  std::string name;

  ModuleInstance(Module* source_, const std::string& name_) :
    source(source_), name(name_) {}
};

class Wire {
public:
  ModuleInstance* inst;
  std::string portName;
};

class Activation;

class ConnectAndContinue {
public:
  bool isStartAction;
  pair<Wire*, Wire*> connection;
  std::vector<Activation*> continuations;
};

typedef ConnectAndContinue CC;

class Activation {
public:
  CC* destination;
  Wire* condition;
  int delay;
};

// How are calling conventions different from
// modules? I want them to always be inlined, but structurally
// how are they different? Maybe they arent? A calling convention
// is just a module with no subconventions, which therefore must
// be inlined in to the code?

typedef Module CallingConvention;

class Module {
  std::set<CC*> body;

  std::set<ModuleInstance*> resources;
  std::set<ModuleInstance*> arguments;

  std::set<CallingConvention*> actions;
  std::string name;
  
public:

  Module(const std::string name_) : name(name_) {}

  void addArgument(Module* type, const std::string& name) {
    arguments.insert(new ModuleInstance(type, name));
  }
  
  int numActions() {
    return actions.size();
  }
  
  void addAction(CallingConvention* callingC) {
    assert(callingC->numActions() == 0);
  }
};

class Context {
public:
  
};

// Example: An adder module has one action, which takes
// an adder as its first argument, and which

int main() {
  Module* wire16 = new Module("wire16");
  Module* add16 = new Module("add16");

  Module* add16Inv = new Module("add16Inv");
  add16Inv->addArgument(add16, "adder");
  add16Inv->addArgument(wire16, "in0");

  add16->addAction(add16Inv);
}
