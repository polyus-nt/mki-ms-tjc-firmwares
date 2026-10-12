// Исполняем patched upstream LED на модельных регистрах, не заменяя его класс.
#include <cassert>
#include <cstdint>
#include "../firmware/lmp/BoardConfig.h"
using byte=uint8_t;
// Даже если latch оставлен HIGH, конструктор обязан сбросить его ДО output-mode.
static uint32_t levels=(1u<<5)|(1u<<6)|(1u<<7);
static char writes[32];
static unsigned count=0;
struct Register {
  uint32_t value=0;
  bool latch;
  explicit Register(bool isLatch=false):latch(isLatch) {}
  operator uint32_t() const { return latch ? 0 : value; } // BSRR reads as zero.
  Register& operator=(uint32_t next) {
    writes[count++]=latch ? 'L' : 'M';
    if(latch) { levels|=next&0xffff;levels&=~(next>>16); }
    else for(unsigned pin=5;pin<=7;++pin) {
      if(((next>>(2*pin))&3)==1 && ((value>>(2*pin))&3)!=1)
        assert(!!(levels&(1u<<pin))==tjc_lmp::startupHigh(pin-5));
    }
    value=next;return *this;
  }
  Register& operator|=(uint32_t next) { return *this=uint32_t(*this)|next; }
};
struct Port { Register MODER;uint32_t OTYPER=0,PUPDR=0;Register BSRR{true}; };
struct Clock { uint32_t IOPENR=0; };
static Port gpio;
static Clock clockRegs;
#define GPIOA (&gpio)
#define RCC (&clockRegs)
#define RCC_IOPENR_GPIOAEN 1u
#define GPIO_MODER_MODE0_Pos 0u
#define GPIO_PUPDR_PUPD0_Pos 0u
#define GPIO_OTYPER_OT0 1u
#define GPIO_BSRR_BS0_Pos 0u
#define GPIO_BSRR_BS0 1u
#define GPIO_BSRR_BR0 (1u<<16)
static unsigned long millis() { return 0; }
static void delay(unsigned int) {}
class PWM { public: void write(uint8_t,int8_t) {} };
#include LMP_LED_HEADER
int main() {
  LED first(1),second(2),vacuum(3);
  assert(count==6);
  for(unsigned i=0;i<6;++i)assert(writes[i]==(i%2 ? 'M' : 'L'));
  assert(levels==0);
  assert(!first.value&&!second.value&&!vacuum.value);
  first.off();assert(!(levels&(1u<<5))&& !first.value);
  first.on();assert((levels&(1u<<5))&&first.value);
  vacuum.on();assert((levels&(1u<<7))&&vacuum.value);
  vacuum.off();assert(!(levels&(1u<<7))&&!vacuum.value);
}
