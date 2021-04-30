#include <iostream>

#include "flutter/fml/closure.h"
#include "flutter/fml/thread.h"
#include "flutter/fml/time/time_point.h"

void Foo(fml::TimeDelta dta) {
  std::cout << dta.ToNanoseconds() << std::endl;
}

int main() {
  std::cout << "hello, world\n";

  fml::Thread thread = fml::Thread("xxrl");
  thread.GetTaskRunner()->PostTask([]() { std::cout << "yes, hello\n"; });
  Foo({});
  return 0;
}