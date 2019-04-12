#include "../sio_server.h"
#include "sio_packet.h"
#include "sio_handler.h"

namespace sio {

class server_handler : public handler
{
	server* server_;
	websocketpp::connection_hdl hdl_;

	void pong();
	void on_message(sio::packet const& packet);
	void on_packet(sio::packet const& packet);

	virtual void on_socket_closed(std::string const& nsp);
public:
	server_handler(std::string sid, server *s, websocketpp::connection_hdl hdl);
	virtual ~server_handler();

	virtual void close();
	virtual void on_log(const char* line);
	virtual void on_send(bool bin, shared_ptr<const string> const& payload_ptr);

	virtual asio::io_service& get_io_service();

};
}

