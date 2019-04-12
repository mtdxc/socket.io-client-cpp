#pragma once
#include <functional>
#include "sio_socket.h"
#include <mutex>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
typedef websocketpp::server<websocketpp::config::asio> server_type_no_tls;
#if SIO_TLS
#include <websocketpp/config/asio.hpp>
typedef websocketpp::server<websocketpp::config::asio_tls> server_type_tls;
#endif

namespace sio {
    class server_handler;

    class server
    {
        typedef std::function<void(socket::ptr sock)> socket_listener;
        bool m_ssl;
		server_type_no_tls server_;
#if SIO_TLS
        server_type_tls server_tls;
#endif
        std::mutex mutex_;
        std::map<void*, server_handler*> clients_;
        std::map<std::string, std::list<socket::ptr> > rooms_;
        socket_listener socket_open_, socket_close_;
        template<typename server_type>
        void on_ws_close(server_type *server, websocketpp::connection_hdl hdl);
        template<typename server_type>
        void on_ws_msg(server_type *server, websocketpp::connection_hdl hdl, typename server_type::message_ptr msg);
        template<typename server_type>
        void on_ws_open(server_type *server, websocketpp::connection_hdl hdl);

    public:
        server(bool ssl);
        ~server();

        void send(websocketpp::connection_hdl hdl, const std::string& payload, bool bin = false);
        void close(websocketpp::connection_hdl hdl);
        void log(const char* fmt, ...);
        asio::io_service& get_io_service();

        int broadcast(const std::string& nsp, std::string const& name, message::list const& msglist);
        int broadcast(socket::ptr s, std::string const& name, message::list const& msglist);
        server_handler* get_session(const std::string& sid);
        std::list<socket::ptr> room(const std::string& nsp);

        // api
        void start(int listenPort);

        void set_socket_open_listener(socket_listener const& l) { socket_open_ = l; }
        void set_socket_close_listener(socket_listener const& l) { socket_close_ = l; }
        void notify_socket(socket::ptr s, bool open);
    };



}