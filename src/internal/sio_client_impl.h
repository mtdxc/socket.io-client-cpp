#ifndef SIO_CLIENT_IMPL_H
#define SIO_CLIENT_IMPL_H

#include <cstdint>
#ifdef _WIN32
#define _WEBSOCKETPP_CPP11_THREAD_
//#define _WEBSOCKETPP_CPP11_RANDOM_DEVICE_
#define _WEBSOCKETPP_NO_CPP11_FUNCTIONAL_
#define INTIALIZER(__TYPE__)
#else
#define _WEBSOCKETPP_CPP11_STL_ 1
#define INTIALIZER(__TYPE__) (__TYPE__)
#endif
#include <websocketpp/client.hpp>
#if _DEBUG || DEBUG
#if SIO_TLS
#include <websocketpp/config/debug_asio.hpp>
typedef websocketpp::config::debug_asio_tls client_config_tls;
#endif //SIO_TLS
#include <websocketpp/config/debug_asio_no_tls.hpp>
typedef websocketpp::config::debug_asio client_config;
#else
#if SIO_TLS
#include <websocketpp/config/asio_client.hpp>
typedef websocketpp::config::asio_tls_client client_config_tls;
#endif //SIO_TLS
#include <websocketpp/config/asio_no_tls_client.hpp>
typedef websocketpp::config::asio_client client_config;
#endif //DEBUG

#if SIO_TLS
#include <asio/ssl/context.hpp>
#endif

#include <asio/steady_timer.hpp>
#include <asio/error_code.hpp>
#include <asio/io_service.hpp>

#include <memory>
#include <map>
#include <thread>
#include "../sio_client.h"
#include "sio_packet.h"
#include "sio_handler.h"

namespace sio
{
    using namespace websocketpp;
    
    typedef websocketpp::client<client_config> client_type_no_tls;
#if SIO_TLS
    typedef websocketpp::client<client_config_tls> client_type_tls;
#endif

    struct client_impl : public handler {
    public:

        client_impl(const std::string& uri = std::string());
        ~client_impl();
        
        // Client Functions - such as send, etc.
        void connect(const std::string& uri, const std::map<std::string, std::string>& queryString,
                     const std::map<std::string, std::string>& httpExtraHeaders);
        
        // Closes the connection
        void close();
        
        void sync_close();
        
        void set_reconnect_attempts(unsigned attempts) {m_reconn_attempts = attempts;}
        void set_reconnect_delay(unsigned millis);
        void set_reconnect_delay_max(unsigned millis);
        void on_log(const char* line);

        // listeners and event bindings. (see SYNTHESIS_SETTER below)
        virtual void set_open_listener(client::con_listener const& l) { m_open_listener = l; }
        virtual void set_fail_listener(client::con_listener const& l) { m_fail_listener = l; }
        virtual void set_reconnect_listener(client::reconnect_listener const& l) { m_reconnect_listener = l; }
        virtual void set_reconnecting_listener(client::con_listener const& l) { m_reconnecting_listener = l; }
        virtual void set_close_listener(client::close_listener const& l) { m_close_listener = l; }

        // used by sio::client
        void clear_con_listeners()
        {
            m_open_listener = nullptr;
            m_close_listener = nullptr;
            m_fail_listener = nullptr;
            m_reconnect_listener = nullptr;
            m_reconnecting_listener = nullptr;
        }

        void set_socket_open_listener(client::socket_listener const& l) { m_socket_open_listener = l; }
        void set_socket_close_listener(client::socket_listener const& l) { m_socket_close_listener = l; }

        void clear_socket_listeners()
        {
            m_socket_open_listener = nullptr;
            m_socket_close_listener = nullptr;
        }

        virtual void on_socket_closed(std::string const& nsp) {
            if (m_socket_close_listener) m_socket_close_listener(nsp);
        }
        virtual void on_socket_opened(std::string const& nsp) {
            if (m_socket_open_listener) m_socket_open_listener(nsp);
        }
    protected:
        client::socket_listener m_socket_open_listener;
        client::socket_listener m_socket_close_listener;

        client::con_listener m_open_listener;
        client::con_listener m_fail_listener;
        client::con_listener m_reconnecting_listener;
        client::reconnect_listener m_reconnect_listener;
        client::close_listener m_close_listener;

    public:
        virtual void on_packet(packet const& pack);
        virtual void on_send(bool bin, std::shared_ptr<const std::string> const&  payload_ptr);

        asio::io_service& get_io_service();
    private:
        void run_loop();

        void connect_impl();

        void close_impl(close::status::value const& code,std::string const& reason);
        
        void ping(const asio::error_code& ec);
        
        void timeout_pong(const asio::error_code& ec);

        void timeout_reconnect(asio::error_code const& ec);

        unsigned next_delay() const;
        
        
        //websocket callbacks
        template<class client_type>
        void on_fail(client_type* client, connection_hdl con);
        template<class client_type>
        void on_open(client_type* client, connection_hdl con);
        template<class client_type>
        void on_close(client_type* client, connection_hdl con);
        template<class client_type>
        void on_recv(client_type* client, connection_hdl con, typename client_type::message_ptr msg);

        socket::ptr create_socket(const std::string& nsp);
        //socketio callbacks
        void on_handshake(message::ptr const& message);

        void on_pong();

        void reset_states();

        void clear_timers();

        // Connection pointer for client functions.
        connection_hdl m_con;
        client_type_no_tls m_client;
#if SIO_TLS
        client_type_tls client_tls;
#endif
        // Socket.IO server settings
        std::string m_base_url;
        std::string m_query_string;
        std::map<std::string, std::string> m_http_headers;

        unsigned int m_ping_interval;
        unsigned int m_ping_timeout;
        
        std::unique_ptr<std::thread> m_network_thread;
        
        std::unique_ptr<asio::steady_timer> m_ping_timer;
        std::unique_ptr<asio::steady_timer> m_ping_timeout_timer;
        std::unique_ptr<asio::steady_timer> m_reconn_timer;
        
        bool m_ssl;

        unsigned m_reconn_delay;
        unsigned m_reconn_delay_max;

        unsigned m_reconn_attempts;
        unsigned m_reconn_made;
        
        friend class sio::client;
        friend class sio::socket;
    };
}
#endif // SIO_CLIENT_IMPL_H

