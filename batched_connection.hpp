#pragma once
#include "Socket.hpp"
#include "resource_pool.hpp"
#include "better_cv.hpp"

namespace mutils{
	namespace batched_connection {

		struct batched_connections_impl;
		struct SocketBundle {
			Socket sock;
			condition_variable cv;
			SocketBundle(Socket sock):sock(sock){}
			operator bool() const {return true;}
		};
		
		using SocketPool = ResourcePool<SocketBundle,batched_connections_impl*>;
		using LockedResource = typename SocketPool::LockedResource;
		using WeakResource = typename SocketPool::WeakResource;
		
		struct connection : public ::mutils::connection{
			WeakResource sock;
			connection(batched_connections_impl &);
			connection(const connection&);
			std::size_t recv(std::size_t expected, char * parent_buf);
			std::size_t send(std::size_t expected, char const * const buf);
		};
		
		
		struct batched_connections {
			struct Internals;
			Internals *i;
			batched_connections(const int ip, const int port, const int max_connections);
			batched_connections(const batched_connections&) = delete;
			connection spawn();
			~batched_connections();
		};

		struct receiver {
			ServerSocket ss;
			std::map<std::size_t,std::weak_ptr<connection> > dispatch;

			template<typename T>
			std::future<T> accept(std::function<T (connection)> do_this){
				std::async::launch()
			}
			~receiver();
		};
	}
}
