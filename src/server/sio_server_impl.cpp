#include "sio_server_impl.h"
#include <rapidjson/document.h>
#include <rapidjson/encodedstream.h>
#include <rapidjson/writer.h>
#include "guid.h"
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using namespace sio;
server::server(bool ssl):m_ssl(ssl){
}

server::~server(){
}

void server::log(const char* fmt, ...)
{
    char line[1024] = { 0 };
    va_list vl;
    va_start(vl, fmt);
    vsnprintf(line, sizeof(line) - 1, fmt, vl);
    server_.get_alog().write(websocketpp::log::alevel::app, line);
    va_end(vl);
}

void server::notify_socket(socket::ptr s, bool open) {
    std::string nsp = s->get_namespace();
    auto it = std::find(rooms_[nsp].begin(), rooms_[nsp].end(), s);
    if (open) {
        if(it == rooms_[nsp].end())
            rooms_[nsp].push_back(s);
    }
    else {
        if (it != rooms_[nsp].end())
            rooms_[nsp].erase(it);
    }
    socket_listener l = open ? socket_open_ : socket_close_;
    if (l)
        l(s);
}

asio::io_service& server::get_io_service()
{
    if (m_ssl) {
#if SIO_TLS
        return server_tls.get_io_service();
#endif
    }
    else {
        return server_.get_io_service();
    }
}

void server::start(int listenPort) {
    try {
#if SIO_TLS
        if (m_ssl) {
            server_tls.clear_access_channels(websocketpp::log::alevel::all);
            server_tls.clear_error_channels(websocketpp::log::alevel::all);
            server_tls.set_open_handler(websocketpp::lib::bind(&server::on_ws_open<server_type_tls>, this, &server_tls, ::_1));
            server_tls.set_close_handler(websocketpp::lib::bind(&server::on_ws_close<server_type_tls>, this, &server_tls, ::_1));
            server_tls.set_message_handler(websocketpp::lib::bind(&server::on_ws_msg<server_type_tls>, this, &server_tls, ::_1, ::_2));
        }
        else
#endif
        {
            server_.clear_access_channels(websocketpp::log::alevel::all);
            server_.clear_error_channels(websocketpp::log::alevel::all);

            server_.set_open_handler(websocketpp::lib::bind(&server::on_ws_open<server_type_no_tls>, this, &server_, ::_1));
            server_.set_close_handler(websocketpp::lib::bind(&server::on_ws_close<server_type_no_tls>, this, &server_, ::_1));
            server_.set_message_handler(websocketpp::lib::bind(&server::on_ws_msg<server_type_no_tls>, this, &server_, ::_1, ::_2));
        }
#if SIO_TLS
        if (m_ssl) {
            server_tls.init_asio();
            server_tls.listen(listenPort);
            server_tls.start_accept();
            server_tls.run();
        }
        else
#endif
        {
            server_.init_asio();
            server_.listen(listenPort);
            server_.start_accept();
            server_.run();
        }
        std::cout << "Server Listening on Port : " << listenPort << std::endl;
    }
    catch (websocketpp::exception const & e) {
        std::cout << e.what() << std::endl;
    }
    catch (...) {
        std::cout << "other exception" << std::endl;
    }
}

template<typename server_type>
void server::on_ws_close(server_type *server, websocketpp::connection_hdl hdl)
{
    void* p = hdl.lock().get();
    std::cout << "WsServer on_ws_close : " << p << std::endl;
    server_type::connection_ptr connection = server->get_con_from_hdl(hdl);
    clients.erase(p);
    std::cout << "Number of Clients : " << clients.size() << std::endl;
}

template<typename server_type>
void server::on_ws_open(server_type *server, websocketpp::connection_hdl hdl)
{
    void* p = hdl.lock().get();
    std::cout << "WsServer New Connection Handle : " << p << std::endl;

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
    snprintf(buffer, sizeof(buffer), "%d%s", packet::frame_open, strbuf.GetString());
    std::string handshake(buffer);
    std::cout << "HandShake : " << handshake << std::endl;
    server->send(hdl, handshake, websocketpp::frame::opcode::TEXT);

    /* send connect nessage @see server_handler::on_connect
    // packet::frame_message packet::type_connect
    snprintf(buffer, sizeof(buffer), "%d%d", 4, 0);
    std::string openFrame(buffer);
    std::cout << "Open : " << openFrame << std::endl;
    server->send(hdl, openFrame, websocketpp::frame::opcode::TEXT);
    */
    clients[p] = new server_handler(sid, this, hdl);
    std::cout << "Number of Clients : " << clients.size() << std::endl;
}

template<typename server_type>
void server::on_ws_msg(server_type *server, websocketpp::connection_hdl hdl, typename server_type::message_ptr msg)
{
    void* p = hdl.lock().get();
    std::cout << "WsServer OnWebSocketMessage called with hdl: " << p
        << " message: " << msg->get_payload() << std::endl;
    if (clients.count(p)) {
        clients[p]->on_recv(false, msg->get_payload());
    }
}

void server::send(websocketpp::connection_hdl hdl, const std::string& payload, bool bin /*= false*/)
{
    if (!m_ssl) {
        server_.send(hdl, payload,
            bin ? websocketpp::frame::opcode::BINARY : websocketpp::frame::opcode::TEXT);
    }
    else {
#if SIO_TLS
        server_tls.send(hdl, payload,
            bin ? websocketpp::frame::opcode::BINARY : websocketpp::frame::opcode::TEXT);
#endif
    }
}


void server::close(websocketpp::connection_hdl hdl)
{
    websocketpp::lib::error_code ec;
    std::string reason = "server close";
    if (!m_ssl) {
        server_.close(hdl, websocketpp::close::status::policy_violation, reason, ec);
    }
    else {
#if SIO_TLS
        server_tls.close(hdl, websocketpp::close::status::policy_violation, reason, ec);
#endif
    }
    clients.erase(hdl.lock().get());
}

void server_handler::pong()
{
    packet pong(packet::frame_pong);
    m_packet_mgr.encode(pong,
        [&](bool isBin, std::shared_ptr<const std::string> pongPayLoad)
    {
        server_->send(hdl_, *pongPayLoad, websocketpp::frame::opcode::TEXT);
    });
}

void server_handler::on_connect(std::string& nsp)
{
    packet connect(packet::type_connect, nsp);
    m_packet_mgr.encode(connect);
}

void server_handler::on_message(packet const& packet)
{
    packet::type msgType = packet.get_type();
    std::string nsp = packet.get_nsp();
    message::ptr const msg = packet.get_message();

    switch (msgType)
    {
    case packet::type_connect:
        if (!get_socket_locked(nsp)) {
            lock_guard<mutex> guard(m_socket_mutex);
            m_sockets[nsp].reset(new sio::socket(this, nsp));
        }
        on_connect(nsp);
        break;
    case packet::type_disconnect:
        break;
    case packet::type_event:
    case packet::type_binary_event:
    case packet::type_ack:
    case packet::type_binary_ack:
    case packet::type_error:
    case packet::type_undetermined:
    default:
        handler::on_packet(packet);
        break;
    }
}

void server_handler::on_packet(sio::packet const& packet)
{
    std::cout << "Packet Decoded " << packet.get_frame()
        << " data : " << packet.get_message()
        << " NameSpace :" << packet.get_nsp()
        << std::endl;

    switch (packet.get_frame())
    {
    case packet::frame_open:
        log("recv open");
        break;
    case packet::frame_close:
        log("recv close");
        break;
    case packet::frame_ping:
        // log("recv ping");
        pong();
        break;
    case packet::frame_pong:
        // log("recv pong");
        break;
    case packet::frame_message:
        log("recv message %d", packet.get_type());
        on_message(packet);
        break;
    case packet::frame_upgrade:
        break;
    case packet::frame_noop:
        break;
    default:
        log("unknown message %d", packet.get_frame());
        break;;
    }
}

void server_handler::on_send(bool bin, shared_ptr<const string> const& payload_ptr)
{
    if (server_ && hdl_.lock())
        server_->send(hdl_, *payload_ptr, bin);
}
