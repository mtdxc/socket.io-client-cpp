#ifndef SIO_SOCKET_H
#define SIO_SOCKET_H
#include "sio_message.h"
#include <mutex>
#include <functional>
namespace sio
{
    class event_adapter;
    
    class event
    {
    public:
        const std::string& get_nsp() const;
        
        const std::string& get_name() const;
        // return first message in get_messages
        const message::ptr& get_message() const;

        const message::list& get_messages() const;
        
        bool need_ack() const;
        
        void put_ack_message(message::list const& ack_message);
        
        message::list const& get_ack_message() const;


        event(std::string const& nsp,std::string const& name,message::list const& messages,bool need_ack);
        event(std::string const& nsp,std::string const& name,message::list&& messages,bool need_ack);
    protected:
        message::list& get_ack_message_impl();
        
    private:
        const std::string m_nsp;
        const std::string m_name;
        const message::list m_messages;
        const bool m_need_ack;
        message::list m_ack_message;
        
        friend class event_adapter;
    };
    
    class handler;
    class packet;
    
    //The name 'socket' is taken from concept of official socket.io.
    class socket
    {
    public:
        typedef std::function<void(const std::string& name,message::ptr const& message,bool need_ack, message::list& ack_message)> event_listener_aux;
        
        typedef std::function<void(event& event)> event_listener;
        
        typedef std::function<void(message::ptr const& message)> error_listener;
        
        typedef std::shared_ptr<socket> ptr;
        
        void on(std::string const& event_name,event_listener const& func);
        void on(std::string const& event_name,event_listener_aux const& func);        
        void off(std::string const& event_name);
        void off_all();
        
        void on_error(error_listener const& l);
        void off_error();

        void emit(std::string const& name, message::list const& msglist = nullptr, std::function<void (message::list const&)> const& ack = nullptr);

        std::string const& get_namespace() const;

        socket(handler* h, const std::string& n) :m_client(h), m_nsp(n) {}

        virtual void close() {}
    protected:

        friend class handler;
        // sio消息触发
        virtual void on_connected() {}
        virtual void on_close();
        // socket连接触发
        virtual void on_open() {}
        virtual void on_disconnect() {}

        virtual void send_packet(packet& p);
        void on_message_packet(packet const& p);
        // Message Parsing callbacks.
        void on_socketio_event(const std::string& nsp, int msgId, const std::string& name, message::list&& message);
        void on_socketio_ack(int msgId, message::list const& message);
        void on_socketio_error(message::ptr const& err_message);

        void ack(int msgId, std::string const& name, message::list const& ack_message);

        sio::handler *m_client;
        std::string m_nsp;
        static unsigned int s_global_event_id;
        // event ack map : packet_id -> callback
        std::map<unsigned int, std::function<void(message::list const&)> > m_acks;

        // event callback map
        std::map<std::string, event_listener> m_event_binding;
        std::mutex m_event_mutex;
        event_listener get_bind_listener_locked(std::string const& event);

        error_listener m_error_listener;
    private:
        //disable copy constructor and assign operator.
        socket(socket const&){}
        void operator=(socket const&){}

    };
}
#endif // SIO_SOCKET_H
