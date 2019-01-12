//
//  sio_client_impl.cpp
//  SioChatDemo
//
//  Created by Melo Yao on 4/3/15.
//  Copyright (c) 2015 Melo Yao. All rights reserved.
//

#include "sio_client_impl.h"
#include <functional>
#include <chrono>
#include <mutex>
#include <cmath>

#if SIO_TLS
// If using Asio's SSL support, you will also need to add this #include.
// Source: http://think-async.com/Asio/asio-1.10.6/doc/asio/using.html
// #include <asio/ssl/impl/src.hpp>
#endif

using std::chrono::milliseconds;
using namespace std;
#define NULL_GUARD(_x_)  \
    if(_x_ == NULL) return
namespace sio
{
	std::string encode_query_string(const std::string &query)
	{
		ostringstream ss;
		ss << std::hex;
		// Percent-encode (RFC3986) non-alphanumeric characters.
		for (const char c : query) {
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
				ss << c;
			}
			else {
				ss << '%' << std::uppercase << std::setw(2) << int((unsigned char)c) << std::nouppercase;
			}
		}
		ss << std::dec;
		return ss.str();
	}
    class client_socket : public socket
    {
    public:

        client_socket(handler *, std::string const&);
        ~client_socket();

        void close();

    protected:
        void on_connected();

        void on_close();

        void on_open();

        void on_disconnect();

        void send_packet(packet& p);
    private:

        void timeout_connection(const asio::error_code &ec);
        void timeout_close(const asio::error_code& error);

        void send_connect();

        std::unique_ptr<asio::steady_timer> m_connection_timer;
        bool m_connected;

        std::list<packet> m_packet_queue;
        std::mutex m_packet_mutex;

    };


    client_socket::client_socket(handler* client, std::string const& nsp) :
        socket(client, nsp),
        m_connected(false)
    {
        NULL_GUARD(client);
        if (m_client->opened())
        {
            send_connect();
        }
    }

    client_socket::~client_socket()
    {
    }

    void client_socket::send_connect()
    {
        NULL_GUARD(m_client);
        if (m_nsp == "/")
        {
            return;
        }
        packet p(packet::type_connect, m_nsp);
        m_client->send(p);
        m_connection_timer.reset(new asio::steady_timer(m_client->get_io_service()));
        asio::error_code ec;
        m_connection_timer->expires_from_now(std::chrono::milliseconds(20000), ec);
        m_connection_timer->async_wait(std::bind(&client_socket::timeout_connection, this, std::placeholders::_1));
    }

    void client_socket::close()
    {
        NULL_GUARD(m_client);
        if (m_connected)
        {
            packet p(packet::type_disconnect, m_nsp);
            send_packet(p);

            if (!m_connection_timer) {
                m_connection_timer.reset(new asio::steady_timer(m_client->get_io_service()));
            }
            asio::error_code ec;
            m_connection_timer->expires_from_now(std::chrono::milliseconds(3000), ec);
            m_connection_timer->async_wait(std::bind(&client_socket::on_close, this));
        }
    }

    void client_socket::on_connected()
    {
        if (m_connection_timer)
        {
            m_connection_timer->cancel();
            m_connection_timer.reset();
        }
        if (!m_connected)
        {
            m_connected = true;
            m_client->on_socket_opened(m_nsp);

            while (true) {
                m_packet_mutex.lock();
                if (m_packet_queue.empty()) {
                    m_packet_mutex.unlock();
                    return;
                }
                sio::packet front_pack = std::move(m_packet_queue.front());
                m_packet_queue.pop_front();
                m_packet_mutex.unlock();
                m_client->send(front_pack);
            }
        }
    }

    void client_socket::on_close()
    {
        NULL_GUARD(m_client);
        sio::handler *client = m_client;
        m_client = NULL;

        if (m_connection_timer)
        {
            m_connection_timer->cancel();
            m_connection_timer.reset();
        }
        m_connected = false;
        {
            std::lock_guard<std::mutex> guard(m_packet_mutex);
            m_packet_queue.clear();
        }
        socket::close();
    }

    void client_socket::on_open()
    {
        send_connect();
    }

    void client_socket::on_disconnect()
    {
        NULL_GUARD(m_client);
        if (m_connected)
        {
            m_connected = false;
            std::lock_guard<std::mutex> guard(m_packet_mutex);
            m_packet_queue.clear();
        }
    }

    void client_socket::timeout_connection(const asio::error_code &ec)
    {
        NULL_GUARD(m_client);
        if (ec)
        {
            return;
        }
        m_connection_timer.reset();
        m_client->log("Connection timeout,close socket.");
        //Should close socket if no connected message arrive.Otherwise we'll never ask for open again.
        this->on_close();
    }

    void client_socket::send_packet(sio::packet &p)
    {
        NULL_GUARD(m_client);
        if (m_connected)
        {
            while (true) {
                m_packet_mutex.lock();
                if (m_packet_queue.empty()) {
                    m_packet_mutex.unlock();
                    break;
                }
                sio::packet front_pack = std::move(m_packet_queue.front());
                m_packet_queue.pop_front();
                m_packet_mutex.unlock();
                m_client->send(front_pack);
            }
            socket::send_packet(p);
            //m_client->send(p);
        }
        else
        {
            std::lock_guard<std::mutex> guard(m_packet_mutex);
            m_packet_queue.push_back(p);
        }
    }

#if SIO_TLS
	typedef websocketpp::lib::shared_ptr<asio::ssl::context> context_ptr;
	static context_ptr on_tls_init(connection_hdl conn)
	{
		context_ptr ctx = context_ptr(new  asio::ssl::context(asio::ssl::context::tlsv1));
		asio::error_code ec;
		ctx->set_options(asio::ssl::context::default_workarounds |
			asio::ssl::context::no_sslv2 |
			asio::ssl::context::single_dh_use, ec);
		if (ec)
		{
			cerr << "Init tls failed,reason:" << ec.message() << endl;
		}

		return ctx;
	}
#endif

    /*************************public:*************************/
    client_impl::client_impl(const string& uri) :
        m_base_url(uri),
        m_ping_interval(0),
        m_ping_timeout(0),
        m_network_thread(),
        m_reconn_delay(5000),
        m_reconn_delay_max(25000),
        m_reconn_attempts(0xFFFFFFFF),
        m_reconn_made(0)
    {
        using websocketpp::log::alevel;
#ifndef DEBUG
        m_client.clear_access_channels(alevel::all);
        m_client.set_access_channels(alevel::connect|alevel::disconnect|alevel::app);
#endif
        // Initialize the Asio transport policy
        m_client.init_asio();

        using std::placeholders::_1;
        using std::placeholders::_2;
        m_client.set_open_handler(std::bind(&client_impl::on_open<client_type_no_tls>,this,&m_client,_1));
        m_client.set_close_handler(std::bind(&client_impl::on_close<client_type_no_tls>,this, &m_client, _1));
        m_client.set_fail_handler(std::bind(&client_impl::on_fail<client_type_no_tls>,this, &m_client, _1));
        m_client.set_message_handler(std::bind(&client_impl::on_recv<client_type_no_tls>,this, &m_client, _1,_2));
#if SIO_TLS
        client_tls.init_asio();
        client_tls.set_open_handler(std::bind(&client_impl::on_open<client_type_tls>, this, &client_tls, _1));
        client_tls.set_close_handler(std::bind(&client_impl::on_close<client_type_tls>, this, &client_tls, _1));
        client_tls.set_fail_handler(std::bind(&client_impl::on_fail<client_type_tls>, this, &client_tls, _1));
        client_tls.set_message_handler(std::bind(&client_impl::on_recv<client_type_tls>, this, &client_tls, _1, _2));

        client_tls.set_tls_init_handler(&on_tls_init);
#endif
    }

    client_impl::~client_impl()
    {
        sockets_invoke_void(socket_on_close());
        sync_close();
    }

    void client_impl::connect(const string& uri, const map<string,string>& query, const map<string, string>& headers)
    {
        if(m_reconn_timer)
        {
            m_reconn_timer->cancel();
            m_reconn_timer.reset();
        }
        if(m_network_thread)
        {
            if(m_con_state == con_closing||m_con_state == con_closed)
            {
                //if client is closing, join to wait.
                //if client is closed, still need to join,
                //but in closed case,join will return immediately.
                m_network_thread->join();
                m_network_thread.reset();//defensive
            }
            else
            {
                //if we are connected, do nothing.
                return;
            }
        }
        m_con_state = con_opening;
        m_reconn_made = 0;
        if(!uri.empty())
        {
            m_base_url = uri;
        }
				m_ssl = handler::is_tls(m_base_url);

        string query_str;
        for(map<string,string>::const_iterator it=query.begin();it!=query.end();++it){
            query_str.append("&");
            query_str.append(it->first);
            query_str.append("=");
            query_str.append(encode_query_string(it->second));
        }
        m_query_string = move(query_str);

        m_http_headers = headers;

        this->reset_states();
        get_io_service().dispatch(std::bind(&client_impl::connect_impl,this));
        m_network_thread.reset(new thread(std::bind(&client_impl::run_loop,this)));//uri lifecycle?

    }

    void client_impl::close()
    {
        m_con_state = con_closing;
        sockets_invoke_void(&sio::socket::close);
        get_io_service().dispatch(std::bind(&client_impl::close_impl, this,close::status::normal,"End by user"));
    }

    void client_impl::sync_close()
    {
        m_con_state = con_closing;
        sockets_invoke_void(&sio::socket::close);
        get_io_service().dispatch(std::bind(&client_impl::close_impl, this,close::status::normal,"End by user"));
        if(m_network_thread)
        {
            m_network_thread->join();
            m_network_thread.reset();
        }
    }

		void client_impl::set_reconnect_delay(unsigned millis)
		{
			m_reconn_delay = millis; 
			if (m_reconn_delay_max < millis) 
				m_reconn_delay_max = millis;
		}

		void client_impl::set_reconnect_delay_max(unsigned millis)
		{
			m_reconn_delay_max = millis;
			if (m_reconn_delay > millis)
				m_reconn_delay = millis;
		}

		/*************************protected:*************************/
		asio::io_service& client_impl::get_io_service()
    {
			if (m_ssl) {
#if SIO_TLS
				return client_tls.get_io_service();
#endif
			}
			else {
				return m_client.get_io_service();
			}
    }

    /*************************private:*************************/
    void client_impl::run_loop()
    {
			if (m_ssl) {
#if SIO_TLS
				client_tls.run();
				client_tls.reset();
#endif
			}
			else {
				m_client.run();
				m_client.reset();

			}
      m_client.get_alog().write(websocketpp::log::alevel::devel, "run loop end");
    }

    void client_impl::connect_impl()
    {
        do{
            ostringstream ss;
            if(m_ssl)
            {
                // This requires SIO_TLS to have been compiled in.
                ss<<"wss://";
            }
            else
            {
                ss<<"ws://";
            }

            websocketpp::uri uo(m_base_url);
            const std::string host(uo.get_host());
            // As per RFC2732, literal IPv6 address should be enclosed in "[" and "]".
            if(host.find(':')!=std::string::npos){
                ss<<"["<<uo.get_host()<<"]";
            } else {
                ss<<uo.get_host();
            }
            ss<<":"<<uo.get_port()<<"/socket.io/?EIO=4&transport=websocket";
            if(m_sid.size()>0){
                ss<<"&sid="<<m_sid;
            }
            ss<<"&t="<<time(NULL)<<m_query_string;
            lib::error_code ec;
						if (m_ssl) {
#if SIO_TLS
							typename client_type_tls::connection_ptr con = client_tls.get_connection(ss.str(), ec);
							if (ec) {
								log("Get Connection Error: %s", ec.message().c_str());
								break;
							}

							for (auto&& header : m_http_headers) {
								con->replace_header(header.first, header.second);
							}
							client_tls.connect(con);
#endif
						}
						else {
							typename client_type_no_tls::connection_ptr con = m_client.get_connection(ss.str(), ec);
							if (ec) {
								log("Get Connection Error: %s", ec.message().c_str());
								break;
							}

							for (auto&& header : m_http_headers) {
								con->replace_header(header.first, header.second);
							}
							m_client.connect(con);
						}
            return;
        }
        while(0);
        if(m_fail_listener)
        {
            m_fail_listener();
        }
    }

    void client_impl::close_impl(close::status::value const& code,string const& reason)
    {
        log("Close by reason: %s", reason.c_str());
        if(m_reconn_timer)
        {
            m_reconn_timer->cancel();
            m_reconn_timer.reset();
        }
        if (m_con.expired())
        {
            log("Error: No active session");
        }
        else
        {
            lib::error_code ec;
            m_client.close(m_con, code, reason, ec);
        }
    }

    void client_impl::on_send(bool bin, shared_ptr<const string> const& payload_ptr)
    {
        if(m_con_state == con_opened)
        {
            //delay the ping, since we already have message to send.
            std::error_code timeout_ec;
            if(m_ping_timer)
            {
                m_ping_timer->expires_from_now(milliseconds(m_ping_interval),timeout_ec);
                m_ping_timer->async_wait(lib::bind(&client_impl::ping,this,lib::placeholders::_1));
            }
            lib::error_code ec;
            frame::opcode::value opcode = bin ? frame::opcode::binary : frame::opcode::text;
            if (m_ssl)
                client_tls.send(m_con, *payload_ptr, opcode, ec);
            else
                m_client.send(m_con, *payload_ptr, opcode, ec);
            if (ec)
            {
                cerr << "Send failed,reason:" << ec.message() << endl;
            }
        }
    }

    void client_impl::ping(const asio::error_code& ec)
    {
        if(ec || m_con.expired())
        {
            if (ec != asio::error::operation_aborted)
                log("ping exit,con is expired? %d, ec:%s", m_con.expired(), ec.message().c_str());
            return;
        }
        packet p(packet::frame_ping);
        m_packet_mgr.encode(p, [&](bool /*isBin*/,shared_ptr<const string> payload)
        {
					lib::error_code ec;
					frame::opcode::value opcode = frame::opcode::text;
					if (m_ssl)
						client_tls.send(m_con, *payload, opcode, ec);
					else
						m_client.send(m_con, *payload, opcode, ec);
        });
        if(m_ping_timer)
        {
            asio::error_code e_code;
            m_ping_timer->expires_from_now(milliseconds(m_ping_interval), e_code);
            m_ping_timer->async_wait(std::bind(&client_impl::ping,this, std::placeholders::_1));
        }
        if(!m_ping_timeout_timer)
        {
            m_ping_timeout_timer.reset(new asio::steady_timer(get_io_service()));
            std::error_code timeout_ec;
            m_ping_timeout_timer->expires_from_now(milliseconds(m_ping_timeout), timeout_ec);
            m_ping_timeout_timer->async_wait(std::bind(&client_impl::timeout_pong, this, std::placeholders::_1));
        }
    }

    void client_impl::timeout_pong(const std::error_code &ec)
    {
        if(ec)
        {
            return;
        }
        log("Pong timeout");
        get_io_service().dispatch(std::bind(&client_impl::close_impl,this,close::status::policy_violation,"Pong timeout"));
    }

    void client_impl::timeout_reconnect(std::error_code const& ec)
    {
        if(ec)
        {
            return;
        }
        if(m_con_state == con_closed)
        {
            m_con_state = con_opening;
            m_reconn_made++;
            reset_states();
            log("Reconnecting...");
            if(m_reconnecting_listener) m_reconnecting_listener();
            get_io_service().dispatch(std::bind(&client_impl::connect_impl,this));
        }
    }

    unsigned client_impl::next_delay() const
    {
        //no jitter, fixed power root.
        unsigned reconn_made = min<unsigned>(m_reconn_made,32);//protect the pow result to be too big.
        return static_cast<unsigned>(min<double>(m_reconn_delay * pow(1.5,reconn_made), m_reconn_delay_max));
    }

    template<typename client_type>
    void client_impl::on_fail(client_type* client, connection_hdl con)
    {
        m_con.reset();
        m_con_state = con_closed;
        sockets_invoke_void(socket_on_disconnect());
        log("Connection failed.");
        if(m_reconn_made<m_reconn_attempts)
        {
            log("Reconnect for attempt:%d",m_reconn_made);
            unsigned delay = next_delay();
            if(m_reconnect_listener) m_reconnect_listener(m_reconn_made, delay);
            m_reconn_timer.reset(new asio::steady_timer(get_io_service()));
            asio::error_code ec;
            m_reconn_timer->expires_from_now(milliseconds(delay), ec);
            m_reconn_timer->async_wait(std::bind(&client_impl::timeout_reconnect,this, std::placeholders::_1));
        }
        else
        {
            if(m_fail_listener) m_fail_listener();
        }
    }

    template<typename client_type>
    void client_impl::on_open(client_type* client, connection_hdl con)
    {
        log("Connected.");
        m_con_state = con_opened;
        m_con = con;
        m_reconn_made = 0;
        sockets_invoke_void(socket_on_open());
        socket("");
        if(m_open_listener) m_open_listener();
    }

    template<typename client_type>
    void client_impl::on_close(client_type* client, connection_hdl con)
    {
        log("Client Disconnected.");
        con_state m_con_state_was = m_con_state;
        m_con_state = con_closed;
        lib::error_code ec;
        typename client_type::connection_ptr conn_ptr  = client->get_con_from_hdl(con, ec);
        close::status::value code = close::status::normal;
        if (ec) {
            log("on_close get conn failed");
        }
        else
        {
            code = conn_ptr->get_local_close_code();
        }
        
        m_con.reset();
        clear_timers();
        client::close_reason reason;

        // If we initiated the close, no matter what the close status was,
        // we'll consider it a normal close. (When using TLS, we can
        // sometimes get a TLS Short Read error when closing.)
        if(code == close::status::normal || m_con_state_was == con_closing)
        {
            sockets_invoke_void(socket_on_disconnect());
            reason = client::close_reason_normal;
        }
        else
        {
            sockets_invoke_void(socket_on_disconnect());
            if(m_reconn_made<m_reconn_attempts)
            {
                log("Reconnect for attempt: %d", m_reconn_made);
                unsigned delay = next_delay();
                if(m_reconnect_listener) m_reconnect_listener(m_reconn_made,delay);
                m_reconn_timer.reset(new asio::steady_timer(get_io_service()));
                asio::error_code ec;
                m_reconn_timer->expires_from_now(milliseconds(delay), ec);
                m_reconn_timer->async_wait(std::bind(&client_impl::timeout_reconnect,this, std::placeholders::_1));
                return;
            }
            reason = client::close_reason_drop;
        }
        
        if(m_close_listener)
        {
            m_close_listener(reason);
        }
    }

    template<typename client_type>
    void client_impl::on_recv(client_type* client, connection_hdl con, typename client_type::message_ptr msg)
    {
        if (m_ping_timeout_timer) {
            asio::error_code ec;
            m_ping_timeout_timer->expires_from_now(milliseconds(m_ping_timeout),ec);
            m_ping_timeout_timer->async_wait(std::bind(&client_impl::timeout_pong, this, std::placeholders::_1));
        }
        // Parse the incoming message according to socket.IO rules
				handler::on_recv(false, msg->get_payload());
    }

    socket::ptr const& client_impl::socket(string const& nsp)
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
            // new client_socket(this, aux);
            pair<const string, socket::ptr> p(aux, shared_ptr<sio::socket>(new client_socket(this, aux)));
            return (m_sockets.insert(p).first)->second;
        }
    }

    void client_impl::on_handshake(message::ptr const& message)
    {
        if(message && message->get_flag() == message::flag_object)
        {
            const object_message* obj_ptr =static_cast<object_message*>(message.get());
            const map<string,message::ptr>* values = &(obj_ptr->get_map());
            auto it = values->find("sid");
            if (it!= values->end()) {
                m_sid = static_pointer_cast<string_message>(it->second)->get_string();
            }
            else
            {
                goto failed;
            }

            it = values->find("pingInterval");
            if (it!= values->end()&&it->second->get_flag() == message::flag_integer) {
                m_ping_interval = (unsigned)static_pointer_cast<int_message>(it->second)->get_int();
            }
            else
            {
                m_ping_interval = 25000;
            }

            it = values->find("pingTimeout");
            if (it!=values->end()&&it->second->get_flag() == message::flag_integer) {
                m_ping_timeout = (unsigned) static_pointer_cast<int_message>(it->second)->get_int();
            }
            else
            {
                m_ping_timeout = 60000;
            }

            asio::error_code ec;
            m_ping_timer.reset(new asio::steady_timer(get_io_service()));
            m_ping_timer->expires_from_now(milliseconds(m_ping_interval), ec);
            //if(ec) LOG("ec:"<<ec.message()<<endl);
            m_ping_timer->async_wait(std::bind(&client_impl::ping,this, std::placeholders::_1));
            log("On handshake,sid:%s, ping interval:%d, ping timeout %d", m_sid.c_str(), m_ping_interval, m_ping_timeout);
            return;
        }
failed:
        //just close it.
        get_io_service().dispatch(std::bind(&client_impl::close_impl, this,close::status::policy_violation,"Handshake error"));
    }

    void client_impl::on_pong()
    {
        if(m_ping_timeout_timer)
        {
            m_ping_timeout_timer->cancel();
            m_ping_timeout_timer.reset();
        }
    }

    void client_impl::on_packet(packet const& p)
    {
        switch(p.get_frame())
        {
        case packet::frame_open:
            on_handshake(p.get_message());
            break;
        case packet::frame_close:
            //FIXME how to deal?
            close_impl(close::status::abnormal_close, "End by server");
            break;
        case packet::frame_pong:
            on_pong();
            break;

        default:
						handler::on_packet(p);
            break;
        }
    }

    void client_impl::clear_timers()
    {
        log("clear timers");
        asio::error_code ec;
        if(m_ping_timeout_timer)
        {
            m_ping_timeout_timer->cancel(ec);
            m_ping_timeout_timer.reset();
        }
        if(m_ping_timer)
        {
            m_ping_timer->cancel(ec);
            m_ping_timer.reset();
        }
    }

    void client_impl::reset_states()
    {
        m_client.reset();
#if ASIO_TLS
				client_tls.reset();
#endif
        m_sid.clear();
        m_packet_mgr.reset();
    }
}
