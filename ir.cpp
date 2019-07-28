#include "ir.h"

using namespace CAC;

namespace CAC {
  Port getPort(Module* const mod, const std::string& name) {
    return mod->pt(name);
  }


}
