#include "ram.h"

void read_add_2_ram(ram_32_128* ram) {
  int data = read(ram, 10);
  write(ram, 12, data + 2);
}
