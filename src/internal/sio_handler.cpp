#include "sio_handler.h"
#include <websocketpp/uri.hpp>
#include <functional>

#ifdef WIN32
#define strcasecmp _stricmp 
#endif
using namespace std;
using std::placeholders::_1;
using std::placeholders::_2;
namespace sio
{
	void handler::log(const char* fmt, ...)
	{
		char line[1024] = { 0 };
		va_list vl;
		va_start(vl, fmt);
		vsnprintf(line, sizeof(line) - 1, fmt, vl);
		on_log(line);
		va_end(vl);
	}

	handler::handler() :m_con_state(con_closed)
	{
		m_packet_mgr.set_decode_callback(std::bind(&handler::on_packet, this, _1));
		m_packet_mgr.set_encode_callback([this](bool bin, shared_ptr<const string> const& payload) {
			get_io_service().dispatch(std::bind(&handler::on_send, this, bin, payload));
		});
	}

	void handler::remove_socket(std::string const& nsp)
	{
		lock_guard<mutex> guard(m_socket_mutex);
		auto it = m_sockets.find(nsp);
		if (it != m_sockets.end())
		{
			m_sockets.erase(it);
		}
	}

	socket::ptr handler::create_socket(const std::string& nsp)
	{
		socket::ptr p(new sio::socket(this, nsp));
		return p;
	}

	socket::ptr const& handler::socket(string const& nsp)
	{
		lock_guard<mutex> guard(m_socket_mutex);
		string aux;
		if (nsp.empty() || nsp[0] != '/')
		{
			aux = "/" + nsp;
		}
		else
		{
			aux = nsp;
		}

		auto it = m_sockets.find(aux);
		if (it != m_sockets.end())
		{
			return it->second;
		}
		else
		{
			pair<const string, socket::ptr> p(aux, create_socket(aux));
			return (m_sockets.insert(p).first)->second;
		}
	}

	socket::ptr handler::get_socket_locked(string const& nsp)
	{
		lock_guard<mutex> guard(m_socket_mutex);
		auto it = m_sockets.find(nsp);
		if (it != m_sockets.end())
		{
			return it->second;
		}
		else
		{
			return socket::ptr();
		}
	}

	void handler::sockets_invoke_void(void (sio::socket::*fn)(void))
	{
		map<const string, socket::ptr> socks;
		{
			lock_guard<mutex> guard(m_socket_mutex);
			socks.insert(m_sockets.begin(), m_sockets.end());
		}
		for (auto it = socks.begin(); it != socks.end(); ++it) {
			((*(it->second)).*fn)();
		}
	}

	void handler::on_recv(bool bin, const std::string& msg)
	{
		log("on_recv %d %s", bin, msg.c_str());
		m_packet_mgr.put_payload(msg);
	}

	void handler::on_packet(packet const& p)
	{
		if (p.get_frame() == packet::frame_message) {
			socket::ptr so_ptr = get_socket_locked(p.get_nsp());
			if (so_ptr)
				so_ptr->on_message_packet(p);
		}
	}

	void handler::send(packet& p)
	{
		m_packet_mgr.encode(p);
	}


	bool handler::is_tls(const string& uri)
	{
		websocketpp::uri uo(uri);
		if (!strcasecmp(uo.get_scheme().c_str(), "http") || !strcasecmp(uo.get_scheme().c_str(), "ws"))
		{
			return false;
		}
#if SIO_TLS
		else if (!strcasecmp(uo.get_scheme().c_str(), "https") || !strcasecmp(uo.get_scheme().c_str(), "wss"))
		{
			return true;
		}
#endif
		else
		{
			throw std::runtime_error("unsupported URI scheme");
		}
	}
}