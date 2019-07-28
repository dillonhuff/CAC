#pragma once

#include "ir.h"

void loadLLVMFromFile(CAC::Context& c,
                      const std::string& topFunction,
                      const std::string& filePath);
