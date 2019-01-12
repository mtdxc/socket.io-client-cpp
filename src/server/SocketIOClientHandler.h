#pragma once
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
typedef websocketpp::server<websocketpp::config::asio> server_type_no_tls;
#if SIO_TLS
#include <websocketpp/config/asio.hpp>
typedef websocketpp::server<websocketpp::config::asio_tls> server_type_tls;
#endif

#include "WebSocketServer.h"
#include "../internal/sio_packet.h"
#include "../sio_socket.h"
#include <rapidjson/document.h>
#include <rapidjson/encodedstream.h>
#include <rapidjson/writer.h>
#include "guid.h"

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

class client_handler {
public:
	virtual void OnMessage(websocketpp::connection_hdl hdl, const std::string& msg) = 0;
};
template<typename server_type>
class SocketIOClientHandler : public client_handler
{
  typedef typename server_type::message_ptr message_ptr;
	int clientId;

	sio::packet_manager packetManager;
	server_type* server_;
	websocketpp::connection_hdl hdl_;

	void OnMessage(websocketpp::connection_hdl hdl, message_ptr msg) {
		std::cout << "Client Id " << clientId << " called with hdl: " << hdl.lock().get()
			<< " and message: " << msg->get_payload() << std::endl;
		return OnMessage(hdl, msg->get_payload());
	}
	virtual void OnMessage(websocketpp::connection_hdl hdl, const std::string& msg) {
		packetManager.put_payload(msg);
	}


	void SendPong()
	{
		sio::packet pong(sio::packet::frame_pong);
		packetManager.encode(pong,
			[&](bool isBin, std::shared_ptr<const std::string> pongPayLoad)
		{
			server_->send(hdl_, *pongPayLoad, websocketpp::frame::opcode::TEXT);
		});
	}

	void HandlleConnect(std::string& nsp)
	{
		sio::packet connect(sio::packet::type_connect, nsp);
		packetManager.encode(connect,
			[&](bool isBin, std::shared_ptr<const std::string> connectPayLoad)
		{
			server_->send(hdl_, *connectPayLoad, websocketpp::frame::opcode::TEXT);
		});
	}


	void HandleEvent(sio::packet const& eventPacket)
	{
		const sio::message::ptr ptr = eventPacket.get_message();

		if(ptr->get_flag() ==  sio::message::flag_array)
		{
			const sio::array_message* array_ptr = static_cast<const sio::array_message*>(ptr.get());
			if (array_ptr->get_vector().size() >= 1 && array_ptr->get_vector()[0]->get_flag() == sio::message::flag_string)
			{
				const sio::string_message* name_ptr = static_cast<const sio::string_message*>(array_ptr->get_vector()[0].get());
				sio::message::list mlist;
				for (size_t i = 1;i<array_ptr->get_vector().size();++i)
				{
					mlist.push(array_ptr->get_vector()[i]);
				}
				this->CreateEvent(eventPacket.get_nsp(), eventPacket.get_pack_id(), name_ptr->get_string(), std::move(mlist));
			}
		}

	}


	void CreateEvent(const std::string& nsp, int msgId, const std::string& name, sio::message::list && message)
	{
		bool needAck = msgId >= 0;
		sio::event ev(nsp, name, std::move(message), needAck);
		//auto mmp= (std::map<std::string, sio::message::ptr> &&) ev.get_message()->get_map();
		//std::cout << "Property A" << mmp["propA"]->get_string() << std::endl;
	}

	void HandleMessage(sio::packet const& msgPacket)
	{
		sio::packet::type msgType = msgPacket.get_type();
		std::string nameSpace = msgPacket.get_nsp();
		sio::message::ptr const msg = msgPacket.get_message();

		switch (msgType)
		{
		case sio::packet::type_connect:
			HandlleConnect(nameSpace);
			break;
		case sio::packet::type_disconnect: break;
		case sio::packet::type_event:
			HandleEvent(msgPacket);
			break;
		case sio::packet::type_ack: break;
		case sio::packet::type_error: break;
		case sio::packet::type_binary_event: break;
		case sio::packet::type_binary_ack: break;
			break;
		case sio::packet::type_undetermined: break;
		default:;
		}
	}


	void OnPacketDecode(sio::packet const& packet)
	{
		std::cout << "Packet Decoded " << packet.get_frame()
			<< " data : " << packet.get_message()
			<< " NameSpace :" << packet.get_nsp()
			<< std::endl;

		switch (packet.get_frame())
		{
		case sio::packet::frame_open:
			std::cout << "recv open" << std::endl;
			break;
		case sio::packet::frame_close: 
			break;
		case sio::packet::frame_ping:
			std::cout << "recv ping : " << std::endl;
			SendPong();
			break;
		case sio::packet::frame_pong: 
			break;
		case sio::packet::frame_message:
			std::cout << "recv message" << " type : " << packet.get_type() << std::endl;
			HandleMessage(packet);
			break;
		case sio::packet::frame_upgrade: break;
		case sio::packet::frame_noop: break;
		default:;
		}
	}

public:

	SocketIOClientHandler(int clientNumber, server_type *server, websocketpp::connection_hdl hdl) : clientId(clientNumber), server_(server), hdl_(hdl)
	{
		
		server_type::connection_ptr connection = server->get_con_from_hdl(hdl);
		//connection->set_message_handler(bind(&SocketIOClientHandler::OnMessage, this, ::_1, ::_2));
		packetManager.set_decode_callback(bind(&SocketIOClientHandler::OnPacketDecode, this, _1));

		// Send Handshake
		rapidjson::Document document;
		document.SetObject();

		rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
		std::string sid = XGUID::CreateGuidString();
		document.AddMember("sid", rapidjson::StringRef(sid.c_str()), allocator);

		rapidjson::Value array(rapidjson::kArrayType);
		document.AddMember("upgrades", array, allocator);
		document.AddMember("pingInterval", 50000, allocator);
		document.AddMember("pingTimeout", 10000, allocator);

		// dump to stringbuffer
		rapidjson::StringBuffer strbuf;
		rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
		document.Accept(writer);

		char buffer[256] = { '\0' };
		snprintf(buffer, sizeof(buffer), "%d%s", 0, strbuf.GetString());
		std::string handshake(buffer);
		std::cout << "HandShake : " << handshake << std::endl;
		server->send(hdl, handshake, websocketpp::frame::opcode::TEXT);

		snprintf(buffer, sizeof(buffer), "%d%d", 4, 0);
		std::string openFrame(buffer);
		std::cout << "Open : " << openFrame << std::endl;
		server->send(hdl, openFrame, websocketpp::frame::opcode::TEXT);
	}

	virtual ~SocketIOClientHandler()
	{
	}
};

