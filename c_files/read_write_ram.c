#include "ram.h"

void read_write_ram(ram_32_128* ram) {
  int data;
  read(ram, 10, &data);
  write(ram, 12, data);
}
