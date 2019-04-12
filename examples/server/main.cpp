// Socket.IO.Server.cpp : Defines the entry point for the console application.
//
#include "../../src/sio_server.h"

int main()
{
	sio::server ws(false);
    ws.set_socket_close_listener([](sio::socket::ptr s){
    });
    ws.set_socket_open_listener([](sio::socket::ptr s) {
        s->on("login", [](sio::event& e) {
            // process with
            e.get_message();
        });
    });
    ws.start(10000);
    return 0;
}

