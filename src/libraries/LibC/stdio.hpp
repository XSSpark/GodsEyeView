#ifndef STDIO_HPP
#define STDIO_HPP
#define COMPORT 0x3f8

#include "itoa.hpp"
#include "string.hpp"
#include <stdarg.h>

static unsigned short* VideoMemory = (unsigned short*)0xb8000;
static int VideoMemoryIndex = 0;
static int NewLineIndex = 0;

extern void indexmng();
extern void puts(char* str);
extern void puti(int num);
extern void putc(int c);
extern void putx(int c);
extern void vprintf(const char* format, va_list v);
extern void printf(const char* format, ...);
extern void clear();
extern void outb(uint16_t port, uint8_t data);
extern uint8_t inb(uint16_t port);
extern void sleep(uint32_t timer_count);
extern void usleep(uint32_t ms);
extern void PCS_play_sound(uint32_t nFrequence);
extern void PCS_nosound();
extern void beep(int time, int frequency);

/*Serials*/
void init_serial();
int transmit_empty();
void log_putc(char c);
void klog(char* str);
#endif
