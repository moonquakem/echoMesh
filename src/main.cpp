#include "BusinessLogic.h"
#include "EventLoop.h"
#include "TcpServer.h"
#include "ThreadPool.h"
#include <iostream>

int main() {
  EventLoop loop;
  TcpServer server(&loop, 8888, 4);

  registerBusinessLogicHandlers();

  server.start();
  loop.loop();

  return 0;
}

