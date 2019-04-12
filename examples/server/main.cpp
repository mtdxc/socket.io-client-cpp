// Socket.IO.Server.cpp : Defines the entry point for the console application.
//
#include <iostream>
#include "../../src/sio_server.h"
using namespace sio;
server ws(false);
int main()
{
	ws.set_socket_close_listener([](socket::ptr s){
		message::ptr o = object_message::create();
		if (s->GetUserData("username")) {
			object_message* obj = (object_message*)o.get();
			obj->insert("numUsers", int_message::create(1));
			obj->insert("username", s->GetUserData("username"));
			ws.broadcast(s, "user left", o);
		}
	});
	ws.set_socket_open_listener([](socket::ptr s) {
		s->on("add user", [=](sio::event& e) {
			// process with
			std::string user = e.get_message()->get_string();
			std::cout << "add user" << user << std::endl;
			message::ptr o = object_message::create();
			object_message* obj = (object_message*)o.get();
			obj->insert("numUsers", int_message::create(1));
			obj->insert("username", user);
			s->SetUserData("username", user.c_str());
			s->emit("login", o);
			ws.broadcast(s, "user joined", o);
		});
		s->on("new message", [=](sio::event& e) {
			std::cout << "new message " << e.get_message()->get_string() << std::endl;
			std::string user = s->GetUserData("username");
			message::ptr o = object_message::create();
			object_message* obj = (object_message*)o.get();
			obj->insert("message", e.get_message()->get_string());
			obj->insert("username", user);
			ws.broadcast(s, "new message", o);
		});
	});
	ws.start(3000);
	return 0;
}

