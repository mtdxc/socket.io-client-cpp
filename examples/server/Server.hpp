#pragma once

#include "scgi/Service.h"
#include "Message.hpp"
#include "SocketNamespace.hpp"
#include "../../src/internal/guid.h"
#include "asio.hpp"
#include <sstream>
#include <regex>

namespace SOCKETIO_SERVERPP_NAMESPACE
{
namespace lib
{

class SocketNamespace;
#ifdef ABC
typedef scgi::Service<asio::local::stream_protocol> scgiserver;
#endif
class Server
{
    public:
    Server(asio::io_service& io_service)
    :io_service_(io_service)
    ,m_reSockIoMsg("^(\\d):([\\d+]*):([^:]*):?(.*)")//, std::regex::perl)
    {
        socket_ = this->of("");

        m_protocols = {"websocket"};

        server_.init_asio(&io_service);
        server_.set_access_channels(websocketpp::log::alevel::none);
        server_.set_error_channels(websocketpp::log::elevel::warn);
        server_.set_message_handler(bind(&Server::onWebsocketMessage, this, _1, _2));
        server_.set_open_handler(bind(&Server::onWebsocketOpen, this, _1));
        server_.set_close_handler(bind(&Server::onWebsocketClose, this, _1));
    }

    void listen(const string& scgi_socket, int websocket_port)
    {
#ifdef ABC
        auto acceptor = std::make_shared<scgiserver::proto::acceptor>(io_service_, scgiserver::proto::endpoint(scgi_socket));
        m_scgiserver.listen(acceptor);
		m_scgiserver.sig_RequestReceived.connect(bind(&Server::onScgiRequest, this, _1));
        m_scgiserver.start_accept();
#endif
        server_.listen(websocket_port);
        server_.start_accept();
    }

    shared_ptr<SocketNamespace> of(const string& nsp)
    {
        auto iter = sock_nsps_.find(nsp);
        if (iter == sock_nsps_.end())
        {
            auto snsp = make_shared<SocketNamespace>(nsp, server_);
            sock_nsps_.insert(std::make_pair(nsp, snsp));
            return snsp;
        }
        else
        {
            return iter->second;
        }
    }

    shared_ptr<SocketNamespace> sockets()
    {
        return socket_;
    }

    private:
#ifdef ABC
	scgiserver          m_scgiserver;
    void onScgiRequest(scgiserver::CRequestPtr req)
    {
        string uri = req->header("REQUEST_URI");
        string uuid = XGUID::CreateGuidString();
//        std::cout << "Req: " << req->header("REQUEST_METHOD") << "  Uri: " << uri << std::endl;

        if (uri.find("/socket.io/1/") == 0)
        {
            std::ostringstream os;
            os << "Status: 200 OK\r\n";
            os << "Content-Type: text/plain\r\n\r\n";
            os << uuid + ":";
            if (m_heartBeat > 0)
            {
                os << m_heartBeat;
            }
            os << ":" << m_closeTime << ":";
            for (const auto& p : m_protocols)
            {
                os << p << ",";
            }

            req->writeData(os.str());
            req->asyncClose(nullptr);
        }
    }
#endif
    
    void onWebsocketOpen(wspp::connection_hdl hdl)
    {
        auto connection = server_.get_con_from_hdl(hdl);
        string uuid = connection->get_resource().substr(23);

        //m_websocketServer.send(hdl, "5::{name:'connect', args={}}", ws::frame::opcode::value::text);
        server_.send(hdl, "1::", wspp::frame::opcode::value::text);

        //    server_.set_timer(10*1000, bind(&SocketIoServer::sendHeartbeart, this, hdl));
        //    server_.set_timer(1*1000, bind(&SocketIoServer::customEvent, this, hdl));
    }

    /*
     * [message type] ':' [message id ('+')] ':' [message endpoint] (':' [message data])
     *
     * 0::/test disconnect a socket to /test endpoint
     * 0 disconnect whole socket
     * 1::/test?my=param
     * 3:<id>:<ep>:<data>
     * 4:<id>:<ep>:<json>
     * 5:<id>:<ep>:<json event>
     * 6:::<id>
     *
     * regex: "\d:\d*\+*:[^:]*:.*"
     */

    void onWebsocketMessage(wspp::connection_hdl hdl, wsserver::message_ptr msg)
    {
        string payload = msg->get_payload();
        if (payload.size() < 3)
            return;

        std::smatch match;
        if (std::regex_match(payload, match, m_reSockIoMsg))
        {
            Message message = {
                false,
                "",
                payload[0] - '0',
                0,
                false,
                match[3],
                match[4]
            };
                        
            auto socket_namespace = sock_nsps_.find(message.endpoint);
            if (socket_namespace == sock_nsps_.end())
            {
                std::cout << "socketnamespace '" << message.endpoint << "' not found" << std::endl;
                return;
            }

            switch(payload[0])
            {
                case '0': // Disconnect
                    break;
                case '1': // Connect
                    // signal connect to matching namespace
                    socket_namespace->second->onSocketIoConnection(hdl);
                    server_.send(hdl, payload, wspp::frame::opcode::value::text);
                    break;
                case '4': // JsonMessage
                    message.isJson = true;
                    // falltrough
                case '3': // Message
                    socket_namespace->second->onSocketIoMessage(hdl, message);
                    break;
                case '5': // Event
                    socket_namespace->second->onSocketIoEvent(hdl, message);
                    break;
            }
        }
    }

    void onWebsocketClose(wspp::connection_hdl hdl)
    {
        for (auto sns : sock_nsps_)
        {
            sns.second->onSocketIoDisconnect(hdl);
        }

    }

    asio::io_service&   io_service_;
    wsserver            server_;
    shared_ptr<SocketNamespace> socket_;
    map<string, shared_ptr<SocketNamespace>> sock_nsps_;
	std::regex			m_reSockIoMsg;
    int                 m_heartBeat = 30;
    int                 m_closeTime = 30;
    vector<string>      m_protocols;
};

}

    using lib::Server;
}
