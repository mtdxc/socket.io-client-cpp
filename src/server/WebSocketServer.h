#pragma once

#include"SocketIOClientHandler.h"

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

class WebSocketServer
{
	int listenPortNumber;
	server_type_no_tls server;

	std::atomic_int numberOfClients;
#if SIO_TLS
	server_type_tls server_tls;
	std::map<void*, client_handler*> clients;
#endif
	template<typename server_type>
	void OnWebSocketConnectionClose(server_type *server, websocketpp::connection_hdl hdl) {
		void* p = hdl.lock().get();
		std::cout << "WsServer WebSocket Connection Closed Handle : "  << p << std::endl;
		server_type::connection_ptr connection =server->get_con_from_hdl(hdl);
		clients.erase(p);
		 std::cout << "Number of Clients : " << clients.size() << std::endl;
	}
	template<typename server_type>
	void OnWebSocketMessage(server_type *server, websocketpp::connection_hdl hdl, typename server_type::message_ptr msg) {
		void* p = hdl.lock().get();
		std::cout << "WsServer OnWebSocketMessage called with hdl: " << p
			<< " message: " << msg->get_payload() << std::endl;
		if (clients.count(p)) {
			clients[p]->OnMessage(hdl, msg->get_payload());
		}
	}

	template<typename server_type>
	void OnWebSocketConnectionOpen(server_type *server, websocketpp::connection_hdl hdl) {
		void* p = hdl.lock().get();
		std::cout << "WsServer OnWebSocketConnectionOpen New Connection Handle : " << p << std::endl;
		auto client = new SocketIOClientHandler<server_type>(numberOfClients++,server, hdl);
		server_type::connection_ptr connection =server->get_con_from_hdl(hdl);
		clients[p] = client;
		std::cout << "Number of Clients : " << clients.size() << std::endl;
	}

public:

	WebSocketServer(int listenPort, bool ssl):listenPortNumber(listenPort)
	{
		if (ssl) {
#if SIO_TLS
			server_tls.clear_access_channels(websocketpp::log::alevel::all);
			server_tls.clear_error_channels(websocketpp::log::alevel::all);
			server_tls.set_message_handler(websocketpp::lib::bind(&WebSocketServer::OnWebSocketMessage<server_type_tls>, this, &server_tls, ::_1, ::_2));
			server_tls.set_open_handler(websocketpp::lib::bind(&WebSocketServer::OnWebSocketConnectionOpen<server_type_tls>, this, &server_tls, ::_1));
			server_tls.set_close_handler(websocketpp::lib::bind(&WebSocketServer::OnWebSocketConnectionClose<server_type_tls>, this, &server_tls, ::_1));
#else
			 return;
#endif
		}
		else {
			server.clear_access_channels(websocketpp::log::alevel::all);
			server.clear_error_channels(websocketpp::log::alevel::all);

			server.set_message_handler(websocketpp::lib::bind(&WebSocketServer::OnWebSocketMessage<server_type_no_tls>, this, &server, ::_1, ::_2));
			server.set_open_handler(websocketpp::lib::bind(&WebSocketServer::OnWebSocketConnectionOpen<server_type_no_tls>, this, &server, ::_1));
			server.set_close_handler(websocketpp::lib::bind(&WebSocketServer::OnWebSocketConnectionClose<server_type_no_tls>, this, &server, ::_1));
		}
	}

	~WebSocketServer()
	{
	}


	void Start() {
		try {
			server.init_asio();
			server.listen(listenPortNumber);
			server.start_accept();

			std::cout << "Server Listening on Port : " << listenPortNumber << std::endl;
			server.run();
		}
		catch (websocketpp::exception const & e) {
			std::cout << e.what() << std::endl;
		}
		catch (...) {
			std::cout << "other exception" << std::endl;
		}

	}

};

