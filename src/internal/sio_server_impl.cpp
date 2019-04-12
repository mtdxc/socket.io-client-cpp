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
    if (!s) return;
    std::string nsp = s->get_namespace();
    mutex_.lock();
    std::list<socket::ptr>& room = rooms_[nsp];
    auto it = std::find(room.begin(), room.end(), s);
    if (open) {
        if(it == room.end())
            room.push_back(s);
    }
    else {
        if (it != room.end())
            room.erase(it);
    }
    socket_listener l = open ? socket_open_ : socket_close_;
    mutex_.unlock();
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
        using websocketpp::log::alevel;
#ifndef DEBUG
        server_.clear_access_channels(alevel::all);
        server_.set_access_channels(alevel::connect | alevel::disconnect | alevel::app);
#endif
#if SIO_TLS
        if (m_ssl) {
            server_tls.set_open_handler(websocketpp::lib::bind(&server::on_ws_open<server_type_tls>, this, &server_tls, ::_1));
            server_tls.set_close_handler(websocketpp::lib::bind(&server::on_ws_close<server_type_tls>, this, &server_tls, ::_1));
            server_tls.set_message_handler(websocketpp::lib::bind(&server::on_ws_msg<server_type_tls>, this, &server_tls, ::_1, ::_2));
        }
        else
#endif
        {
            server_.set_open_handler(websocketpp::lib::bind(&server::on_ws_open<server_type_no_tls>, this, &server_, ::_1));
            server_.set_close_handler(websocketpp::lib::bind(&server::on_ws_close<server_type_no_tls>, this, &server_, ::_1));
            server_.set_message_handler(websocketpp::lib::bind(&server::on_ws_msg<server_type_no_tls>, this, &server_, ::_1, ::_2));
        }

        log("listen on port %d, ssl %d", listenPort, m_ssl);
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
    //server_type::connection_ptr connection = server->get_con_from_hdl(hdl);
    std::unique_lock<std::mutex> l(mutex_);
    if (clients_.count(p)) {
        server_handler* h = clients_[p];
        clients_.erase(p);
        l.unlock();
        h->close();
    }
    log("on_ws_close %p, %d clients remain", p, clients_.size());
}

template<typename server_type>
void server::on_ws_open(server_type *server, websocketpp::connection_hdl hdl)
{
    void* p = hdl.lock().get();

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
    // send frame_open
    char buffer[256] = { '\0' };
    snprintf(buffer, sizeof(buffer), "%d%s", packet::frame_open, strbuf.GetString());
    std::string handshake(buffer);
    log("HandShake %s", handshake.c_str());
    server->send(hdl, handshake, websocketpp::frame::opcode::TEXT);

    // send connect nessage @see server_handler::on_connect
    snprintf(buffer, sizeof(buffer), "%d%d", packet::frame_message, packet::type_connect);
    std::string openFrame(buffer);
    server->send(hdl, openFrame, websocketpp::frame::opcode::TEXT);
    server_handler* sh = new server_handler(sid, this, hdl);
    {
        std::lock_guard<std::mutex> l(mutex_);
        clients_[p] = sh;
        log("on_ws_open %p, %d clients", p, clients_.size());
    }
    socket::ptr s = sh->socket("");
    notify_socket(s, true);
}

template<typename server_type>
void server::on_ws_msg(server_type *server, websocketpp::connection_hdl hdl, typename server_type::message_ptr msg)
{
    void* p = hdl.lock().get();
    if (clients_.count(p)) {
        // log("%p on_ws_msg %s", p, msg->get_payload().c_str());
        clients_[p]->on_recv(false, msg->get_payload());
    }
    else {
        log("%p unknown msg %s", p, msg->get_payload().c_str());
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
    clients_.erase(hdl.lock().get());
}

server_handler* server::get_session(const std::string& sid) {
    std::lock_guard<std::mutex> m(mutex_);
    for (auto it = clients_.begin(); it != clients_.end(); it++)
    {
        if (it->second->get_sessionid() == sid)
            return it->second;
    }
    return NULL;
}

std::list<socket::ptr> server::room(const std::string& nsp) {
    std::list<socket::ptr> ret;
    std::lock_guard<std::mutex> m(mutex_);
    if (rooms_.count(nsp))
        ret = rooms_[nsp];
    return ret;
}

int server::broadcast(const std::string& nsp, std::string const& name, message::list const& msglist) {
    int ret = 0;
    std::lock_guard<std::mutex> m(mutex_);
    if (rooms_.count(nsp)) {
        for (auto s : rooms_[nsp]) {
            s->emit(name, msglist);
            ret++;
        }
    }
    return ret;
}

int server::broadcast(socket::ptr soc, std::string const& name, message::list const& msglist) {
    int ret = 0;
    std::string nsp = soc->get_namespace();
    std::lock_guard<std::mutex> m(mutex_);
    if (rooms_.count(nsp)) {
        for (auto s : rooms_[nsp]) {
            if(s==soc) continue;
            s->emit(name, msglist);
            ret++;
        }
    }
    return ret;
}

///////////////////////////////////////////////
// server_handler
server_handler::server_handler(std::string sid, server *s, websocketpp::connection_hdl hdl) : server_(s), hdl_(hdl)
{
    // ASSERT(server != NULL);
    m_sid = sid;
    m_con_state = con_opened;
}

server_handler::~server_handler()
{
}

void server_handler::on_log(const char* line)
{
    server_->log("%s - %s", m_sid.c_str(), line);
}

asio::io_service& server_handler::get_io_service()
{
    return server_->get_io_service();
}

void server_handler::pong()
{
    packet pong(packet::frame_pong);
    m_packet_mgr.encode(pong);
}

void server_handler::on_message(packet const& p)
{
    std::string nsp = p.get_nsp();
    switch (p.get_type())
    {
    case packet::type_connect:
        if (!get_socket_locked(nsp)) {
            socket::ptr s = socket(nsp);
            log("socket %s connect", nsp.c_str());
            server_->notify_socket(s, true);
            packet conn(packet::type_connect, nsp);
            m_packet_mgr.encode(conn);
        }
        break;
    case packet::type_disconnect:
        if (socket::ptr s = get_socket_locked(nsp)) {
            log("socket %s disconnect", nsp.c_str());
            on_socket_closed(nsp);
            remove_socket(nsp);
        }
        break;
    case packet::type_event:
    case packet::type_binary_event:
    case packet::type_ack:
    case packet::type_binary_ack:
    case packet::type_error:
    case packet::type_undetermined:
    default:
        handler::on_packet(p);
        break;
    }
}

void server_handler::on_packet(sio::packet const& packet)
{
    //log("on packet %d, nsp %s, data %s", packet.get_frame(), packet.get_nsp().c_str(), packet.get_message());
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

void server_handler::close()
{
    if (hdl_.lock()) {
        server_->close(hdl_);
        hdl_.reset();
    }
    std::lock_guard<std::mutex> m(m_socket_mutex);
    for (SocketMap::iterator it = m_sockets.begin(); 
        it != m_sockets.end(); it++) {
        server_->notify_socket(it->second, false);
    }
    m_sockets.clear();
    m_con_state = con_closed;
}

void server_handler::on_socket_closed(std::string const& nsp)
{
    socket::ptr p = get_socket_locked(nsp);
    if(p){
        server_->notify_socket(p, false);
    }
}
