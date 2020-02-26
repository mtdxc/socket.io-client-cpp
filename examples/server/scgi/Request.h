#pragma once
#include <iostream>
#include <queue>
#include <map>

namespace scgi
{

namespace P = std::placeholders;
using std::string;
using std::queue;
using asio::ip::tcp;
using std::map;
using std::function;
typedef std::function<void()> VoidFunc;
template <class SOCKET>
class Request
{
    public:
        Request(asio::io_service& io_service)
			: m_socket(io_service) { }

        template <typename D>
        void writeData(const D& data)
        {
            m_sendBuffer.push(A::buffer(data));
            startWrite();
        }

        const map<string, string>& headers() const
        {
            return m_headermap;
        }

        const string& header(const string& header) const
        {
            auto iter = m_headermap.find(header);
            if (iter == m_headermap.end())
            {
                std::runtime_error("header '" + header + "' not found");
            }
            return iter->second;
        }

        void asyncClose(std::function<void()> cb)
        {
            sig_Closed.push_back(cb);
            if (m_writingAsync || !m_sendBuffer.empty())
            {
                m_closeSocket = true;
            }
            else
            {
                m_socket.close();
            }
        }
        
        SOCKET& socket()
        {
            return m_socket;
        }
        
        void receive(function<void()> cb)
        {
            sig_Received.push_back(cb);
            m_socket.async_receive(
                    A::buffer(m_recv_buffer),
                    std::bind(&Request::handleReceive, this, P::_1, P::_2));
        }
		std::vector<VoidFunc> sig_Closed;
		std::vector<VoidFunc> sig_Received;

    private:
        void handleReceive(const asio::error_code& error, std::size_t bytes_transferred)
        {
            m_headermap = utils::netstring2map(m_recv_buffer);
            m_uri = m_headermap["REQUEST_URI"];

            // Process request
			for(f: sig_Received)
				f();
        }

        
        void startWrite() 
        {
            if(m_writingAsync == false) {
                A::async_write(m_socket, A::buffer(m_sendBuffer.front()),
                        bind(&Request::handleWrite, this, P::_1, P::_2));
                m_sendBuffer.pop();
                m_writingAsync = true;
            }
        }

        void handleWrite(const asio::error_code& /*error*/, size_t /*bytes_transferred*/)
        {
            m_writingAsync = false;
            if (!m_sendBuffer.empty()) {
                startWrite();
            }
            else if (m_closeSocket)
            {
                m_socket.close();
				for (f : sig_Closed)
					f();
            }
        }

        SOCKET                  m_socket;
        queue<asio::const_buffer>  m_sendBuffer;
        bool                    m_writingAsync = false;
        bool                    m_closeSocket = false;
        std::array<char, 4096>  m_recv_buffer;
        map<string, string>     m_headermap;
        string                  m_uri;

};

} // Namespace scgi
