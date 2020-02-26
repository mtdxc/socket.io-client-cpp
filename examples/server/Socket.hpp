#pragma once

#include <config.hpp>
#include <Message.hpp>
#include <Event.hpp>

#include <rapidjson/document.h>

namespace SOCKETIO_SERVERPP_NAMESPACE
{
namespace lib
{

class SocketNamespace;

class Socket
{
    friend SocketNamespace;
public:
    Socket(wsserver& wsserver, const string& nsp, wspp::connection_hdl hdl)
		: server_(wsserver), nsp_(nsp), ws_hdl_(hdl)
    {
    }

    void on(const std::string& event, function<void (const Event& data)> cb)
    {
        events_[event] = cb;
		log("regevent %s", event.c_str());
	}
    
    void send(const string& data)
    {
        string pl = "3::" + nsp_ + ":" + data;
        server_.send(ws_hdl_, pl, wspp::frame::opcode::value::text);
		log("send %s", data.c_str());
    }

    void emit(const string& name, const string& data)
    {
        string pl = "5::" + nsp_ + ":{\"name\":\"" + name + "\",\"args\":["+data+"]}";
        server_.send(ws_hdl_, pl, wspp::frame::opcode::value::text);
		log("emit %s data %s", name.c_str(), data.c_str());
    }

    void onMessage(const Message& msg)
    {
        auto iter = events_.find("message");
        if (iter != events_.end())
        {
			log("onmessage %s", msg.data.c_str());
            iter->second({"message", msg.data});
        }
    }

    void onEvent(const string& event, const rapidjson::Document& json, const string& rawJson)
    {
        auto iter = events_.find(event);
        if (iter != events_.end())
        {
            iter->second({event, json, rawJson});
			log("onEvent %s", event.c_str());
		}
    }

    string uuid() const
    {
        auto connection = server_.get_con_from_hdl(ws_hdl_);
        return connection->get_resource().substr(23);
    }

	void log(const char* fmt, ...) {
		char line[1024] = { 0 };
		va_list vl;
		va_start(vl, fmt);
		vsnprintf(line, sizeof(line) - 1, fmt, vl);
		server_.get_alog().write(websocketpp::log::alevel::app, line);
		va_end(vl);
	}
    wsserver&               server_;
    const string&           nsp_;
    wspp::connection_hdl    ws_hdl_;
    map<string, function<void (const Event&)>> events_;
};

}
    using lib::Socket;
}
