#pragma once
#include <map>
#include <mutex>
#include "../sio_client.h"
#include "sio_packet.h"
#include <asio/io_service.hpp>
namespace sio
{
	struct handler {
		typedef std::map<const std::string, socket::ptr> SocketMap;
		enum con_state
		{
			con_opening,
			con_opened,
			con_closing,
			con_closed
		};
		handler();

		virtual void on_socket_opened(std::string const& nsp) {}
		virtual void on_socket_closed(std::string const& nsp) {}
		bool opened() const { return m_con_state == con_opened; }
		virtual void close() = 0;

		std::string const& get_sessionid() const { return m_sid; }

		void log(const char* fmt, ...);
		virtual void on_log(const char* line) = 0;

		// network recved data
		void on_recv(bool bin, const std::string& msg);
		virtual void on_packet(packet const& pack);
		// used by sio::socket
		void send(packet& p);
		// send to network
		virtual void on_send(bool bin, shared_ptr<const string> const& payload) = 0;

		virtual asio::io_service& get_io_service() = 0;
		// used for selecting whether or not to use TLS
		static bool is_tls(const std::string& uri);

		void remove_socket(std::string const& nsp);
		virtual socket::ptr create_socket(const std::string& nsp);
		socket::ptr const& socket(string const& nsp);
		// const SocketMap& sockets() const { return m_sockets; }
	protected:
		socket::ptr get_socket_locked(string const& nsp);

		typedef void (sio::socket::*socket_void_fn)(void);
		inline socket_void_fn socket_on_close() { return &sio::socket::on_close; }
		inline socket_void_fn socket_on_disconnect() { return &sio::socket::on_disconnect; }
		inline socket_void_fn socket_on_open() { return &sio::socket::on_open; }
		void sockets_invoke_void(void (sio::socket::*fn)(void));

		// nsp->socket::ptr
		SocketMap m_sockets;
		std::mutex m_socket_mutex;

		// for packet encode
		packet_manager m_packet_mgr;
		con_state m_con_state;
		// session id for server
		std::string m_sid;
	};
}
