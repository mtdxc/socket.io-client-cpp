// Socket.IO.Server.cpp : Defines the entry point for the console application.
//

#include "WebSocketServer.h"
int main()
{

	WebSocketServer ws(10000);
	ws.Start();
  return 0;
}

