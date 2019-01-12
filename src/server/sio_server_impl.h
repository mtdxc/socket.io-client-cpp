

#include "sio_server.h"
#include "../internal/sio_packet.h"
#include "../internal/sio_handler.h"
#include "../sio_socket.h"

using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

namespace sio {

class server_handler : public handler
{
	server* server_;
	websocketpp::connection_hdl hdl_;

	socket::ptr const& socket(string const& nsp) {
		return get_socket_locked(nsp);
	}

	void pong();

	void on_connect(std::string& nsp);
	void on_message(sio::packet const& packet);
	void on_packet(sio::packet const& packet);

public:

	server_handler(std::string sid, server *s, websocketpp::connection_hdl hdl) : server_(s), hdl_(hdl)
	{
		// ASSERT(server != NULL);
        m_sid = sid;
	}

	virtual ~server_handler()
	{
	}

	virtual void close() override
	{
        server_->close(hdl_);
	}


	virtual void on_log(const char* line) override
	{
        server_->log("%s - %s", m_sid.c_str(), line);
	}

	virtual void on_send(bool bin, shared_ptr<const string> const& payload_ptr) override;

	virtual asio::io_service& get_io_service() override
	{
		return server_->get_io_service();
	}

};
}

