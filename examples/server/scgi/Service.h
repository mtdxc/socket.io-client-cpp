#pragma once

#include <asio.hpp>
#include "Netstring.h"
#include "Request.h"
#include <memory>

namespace scgi
{
namespace P = std::placeholders;
using std::string;
using std::shared_ptr;

template <class PROTOCOL>
class Service
{
    public:
        using CRequest = Request<typename PROTOCOL::socket>;
        using CRequestPtr = shared_ptr<CRequest>;
		typedef std::function<void(CRequestPtr)> RequestFunc;
        typedef PROTOCOL proto;

        void listen(shared_ptr<typename PROTOCOL::acceptor> acceptor)
        {
            m_acceptor = acceptor;
        }
        
        void start_accept()
        {
            auto request = std::make_shared<CRequest>(m_acceptor->get_io_service());

            m_acceptor->async_accept(request->socket(), bind(&Service::onAccept, this, request, P::_1));
        }

        std::vector<RequestFunc> sig_RequestReceived;

    private:

        void onAccept(CRequestPtr request, const asio::error_code& error)
        {
            start_accept();
            if (!error) {
                request->receive(bind([&](CRequestPtr r) { 
					for(auto f : sig_RequestReceived)
						f(r);
				}, request));
            }
        }

        shared_ptr<typename PROTOCOL::acceptor> m_acceptor;
};

};
