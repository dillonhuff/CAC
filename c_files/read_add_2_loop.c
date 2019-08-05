#include "ram.h"

void read_add_2_ram(ram_32_128* ram) {
  int i;
  for (i = 0; i < 20; i++) {
    int data = read(ram, i);
    write(ram, i + 10, data + 2);
  }
}
