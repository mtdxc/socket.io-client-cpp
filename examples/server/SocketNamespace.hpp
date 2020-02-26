#pragma once

#include <config.hpp>
#include <Server.hpp>
#include <Socket.hpp>

#include <rapidjson/document.h>

namespace SOCKETIO_SERVERPP_NAMESPACE
{
namespace lib
{

class Server;
class Socket;
typedef function<void(Socket&)> SocketFunc;
class SocketNamespace
{
    friend Server;
	std::vector<SocketFunc> sig_Connection;
	std::vector<SocketFunc> sig_Disconnection;
public:
    SocketNamespace(const string& nsp, wsserver& server)
		:nsp_(nsp) ,server_(server)
    {
    }

    void onConnection(SocketFunc cb)
    {
        sig_Connection.push_back(cb);
    }

    void onDisconnection(SocketFunc cb)
    {
        sig_Disconnection.push_back(cb);
    }

    void send(const string& data)
    {
		log("send $s", data.c_str());
        for (const auto& i : sockets_)
        {
            i.second->send(data);
        }
    }

    void emit(const string& name, const string& data)
    {
		log("emit %s:$s", name.c_str(), data.c_str());
        for (const auto& i : sockets_)
        {
            i.second->emit(name, data);
        }
    }

    string socketNamespace() const
    {
        return nsp_;
    }
    
private:

    void onSocketIoConnection(wspp::connection_hdl hdl)
    {
        auto socket = make_shared<Socket>(server_, nsp_, hdl);
        sockets_[hdl] = socket;
		for (auto f : sig_Connection) {
			f(*socket);
		}
    }

    void onSocketIoMessage(wspp::connection_hdl hdl, const Message& msg)
    {
        auto iter = sockets_.find(hdl);
        if (iter != sockets_.end())
        {
            iter->second->onMessage(msg);
        }
    }
                    
    void onSocketIoEvent(wspp::connection_hdl hdl, const Message& msg)
    {
        rapidjson::Document json;
        json.Parse<0>(msg.data.c_str());

        string name = json["name"].GetString();
        auto iter = sockets_.find(hdl);
        if (iter != sockets_.end())
        {
            iter->second->onEvent(name, json, msg.data);
        }

    }

    void onSocketIoDisconnect(wspp::connection_hdl hdl)
    {
        auto iter = sockets_.find(hdl);
        if (iter != sockets_.end())
        {
			for (auto f : sig_Disconnection) {
				f(*(iter->second));
			}
            sockets_.erase(iter);
        }
    }

	void log(const char* fmt, ...) {
		char line[1024] = { 0 };
		va_list vl;
		va_start(vl, fmt);
		vsnprintf(line, sizeof(line) - 1, fmt, vl);
		server_.get_alog().write(websocketpp::log::alevel::app, line);
		va_end(vl);
	}

    string nsp_;
    wsserver& server_;
	map<wspp::connection_hdl, shared_ptr<Socket>, std::owner_less<wspp::connection_hdl>> sockets_;
};

}
    using lib::SocketNamespace;
}
