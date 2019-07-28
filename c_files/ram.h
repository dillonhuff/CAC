#pragma once

typedef struct {
  int data[128];
} ram_32_128;

__attribute__((noinline, optnone))
void read(ram_32_128* ram, int addr, int* data) {
  *data = ram->data[addr];
}

__attribute__((noinline, optnone))
void write(ram_32_128* ram, int addr, int data) {
  ram->data[addr] = data;
}
