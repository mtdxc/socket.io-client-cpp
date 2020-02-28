#include "sio_socket.h"
#include "internal/sio_packet.h"
#include "internal/sio_client_impl.h"
#include <asio/steady_timer.hpp>
#include <asio/error_code.hpp>
#include <list>
#include <chrono>
#include <cstdarg>
#include <functional>

namespace sio
{
    class event_adapter
    {
    public:
        static void adapt_func(socket::event_listener_aux const& func, event& event)
        {
            func(event.get_name(),event.get_message(),event.need_ack(),event.get_ack_message_impl());
        }
        
        static inline socket::event_listener do_adapt(socket::event_listener_aux const& func)
        {
            return std::bind(&event_adapter::adapt_func, func, std::placeholders::_1);
        }
        
        static inline event create_event(std::string const& nsp,std::string const& name,message::list&& message,bool need_ack)
        {
            return event(nsp,name,message,need_ack);
        }
    };
    
    const std::string& event::get_nsp() const
    {
        return m_nsp;
    }
    
    const std::string& event::get_name() const
    {
        return m_name;
    }
    
    const message::ptr& event::get_message() const
    {
        if(m_messages.size()>0)
            return m_messages[0];
        else
        {
            static message::ptr null_ptr;
            return null_ptr;
        }
    }

    const message::list& event::get_messages() const
    {
        return m_messages;
    }
    
    bool event::need_ack() const
    {
        return m_need_ack;
    }
    
    void event::put_ack_message(message::list const& ack_message)
    {
        if(m_need_ack)
            m_ack_message = std::move(ack_message);
    }
    
    event::event(std::string const& nsp,std::string const& name,message::list&& messages,bool need_ack):
        m_nsp(nsp),
        m_name(name),
        m_messages(std::move(messages)),
        m_need_ack(need_ack)
    {
    }

    event::event(std::string const& nsp,std::string const& name,message::list const& messages,bool need_ack):
        m_nsp(nsp),
        m_name(name),
        m_messages(messages),
        m_need_ack(need_ack)
    {
    }
    
    message::list const& event::get_ack_message() const
    {
        return m_ack_message;
    }
    
    message::list& event::get_ack_message_impl()
    {
        return m_ack_message;
    }

    unsigned int socket::s_global_event_id = 1;
    std::string const& socket::get_namespace() const
    {
        return m_nsp;
    }

    int socket::set_user_data(const char* key, void* value) {
        int r = m_user_data.count(key);
        if (value) {
            m_user_data[key] = value;
        }
        else if (r) {
            m_user_data.erase(key);
        }
        return r;
    }

    void* socket::get_user_data(const char* key) {
        void* ret = NULL;
        if (m_user_data.count(key))
            ret = m_user_data[key];
        return ret;
    }

    int socket::clear_user_datas() {
        int r = m_user_data.size();
        m_user_data.clear();
        return r;
    }

    void socket::on(std::string const& event_name, event_listener_aux const& func)
    {
        this->on(event_name, event_adapter::do_adapt(func));
    }

    void socket::on(std::string const& event_name, event_listener const& func)
    {
        std::lock_guard<std::mutex> guard(m_event_mutex);
        m_event_binding[event_name] = func;
    }

    void socket::off(std::string const& event_name)
    {
        std::lock_guard<std::mutex> guard(m_event_mutex);
        auto it = m_event_binding.find(event_name);
        if (it != m_event_binding.end())
        {
            m_event_binding.erase(it);
        }
    }

    void socket::off_all()
    {
        std::lock_guard<std::mutex> guard(m_event_mutex);
        m_event_binding.clear();
    }

    void socket::on_error(error_listener const& l)
    {
        m_error_listener = l;
    }

    void socket::off_error()
    {
        m_error_listener = nullptr;
    }

		socket::event_listener socket::get_bind_listener_locked(const std::string &event)
		{
			std::lock_guard<std::mutex> guard(m_event_mutex);
			auto it = m_event_binding.find(event);
			if (it != m_event_binding.end())
			{
				return it->second;
			}
			return socket::event_listener();
		}

		void socket::emit(std::string const& name, message::list const& msglist, std::function<void(message::list const&)> const& ack)
		{
			message::ptr msg_ptr = msglist.to_array_message(name);
			int pack_id;
			if (ack)
			{
				// need ack insert callback with pack_id to ack map
				pack_id = s_global_event_id++;
				std::lock_guard<std::mutex> guard(m_event_mutex);
				m_acks[pack_id] = ack;
			}
			else
			{
				pack_id = -1;
			}
			packet p(m_nsp, msg_ptr, pack_id);
			send_packet(p);
		}

    void socket::on_message_packet(packet const& p)
    {
        if (p.get_nsp() == m_nsp)
        {
            switch (p.get_type())
            {
                // Connect open
            case packet::type_connect:
            {
                m_client->log("Received Message type (Connect)");
                this->on_connected();
                break;
            }
            case packet::type_disconnect:
            {
                m_client->log("Received Message type (Disconnect)");
                this->on_close();
                break;
            }
            case packet::type_event:
            case packet::type_binary_event:
            {
                m_client->log("Received Message type (Event)");
                const message::ptr ptr = p.get_message();
                if (ptr->get_flag() == message::flag_array)
                {
                    const array_message* array_ptr = static_cast<const array_message*>(ptr.get());
                    if (array_ptr->get_vector().size() >= 1 && array_ptr->get_vector()[0]->get_flag() == message::flag_string)
                    {
                        const string_message* name_ptr = static_cast<const string_message*>(array_ptr->get_vector()[0].get());
                        message::list mlist;
                        for (size_t i = 1; i < array_ptr->get_vector().size(); ++i)
                        {
                            mlist.push(array_ptr->get_vector()[i]);
                        }
                        on_socketio_event(p.get_nsp(), p.get_pack_id(), name_ptr->get_string(), std::move(mlist));
                    }
                }

                break;
            }
            // Ack
            case packet::type_ack:
            case packet::type_binary_ack:
            {
                m_client->log("Received Message type (ACK)");
                const message::ptr ptr = p.get_message();
                if (ptr->get_flag() == message::flag_array)
                {
                    message::list msglist(ptr->get_vector());
                    on_socketio_ack(p.get_pack_id(), msglist);
                }
                else
                {
                    on_socketio_ack(p.get_pack_id(), message::list(ptr));
                }
                break;
            }
            // Error
            case packet::type_error:
            {
                m_client->log("Received Message type (ERROR)");
                on_socketio_error(p.get_message());
                break;
            }
            default:
                break;
            }
        }
    }

    void socket::on_socketio_event(const std::string& nsp, int msgId, const std::string& name, message::list && message)
    {
        bool needAck = msgId >= 0;
        event ev = event_adapter::create_event(nsp, name, std::move(message), needAck);
        event_listener func = get_bind_listener_locked(name);
        if (func) func(ev);
        if (needAck) {
            ack(msgId, name, ev.get_ack_message());
        }
    }

    void socket::ack(int msgId, const string &, const message::list &ack_message)
    {
        packet p(m_nsp, ack_message.to_array_message(), msgId, true);
        send_packet(p);
    }

    void socket::send_packet(packet& p)
    {
        if(m_client)
            m_client->send(p);
    }

    void socket::on_socketio_ack(int msgId, message::list const& message)
    {
        std::function<void(message::list const&)> l;
        {
            std::lock_guard<std::mutex> guard(m_event_mutex);
            auto it = m_acks.find(msgId);
            if (it != m_acks.end())
            {
                l = it->second;
                m_acks.erase(it);
            }
        }
        if (l)l(message);
    }

    void socket::on_socketio_error(message::ptr const& err_message)
    {
        if (m_error_listener) m_error_listener(err_message);
    }

    void socket::on_close()
    {
        if (m_client) {
            m_client->on_socket_closed(m_nsp);
            m_client->remove_socket(m_nsp);
        }
    }
}




