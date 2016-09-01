#pragma once
#include "Socket.hpp"
#include "resource_pool.hpp"
#include <mutex>

namespace mutils{

	struct batched_connections_impl;
	
	using SocketPool = ResourcePool<Socket,batched_connections_impl*>;
	using LockedResource = typename SocketPool::LockedResource;
	using WeakResource = typename SocketPool::WeakResource;

	struct connection{
		struct Internals;
		Internals *i;
		WeakResource sock;
		connection(Internals *i);
		connection(const connection&);
		~connection();
	};


	struct batched_connections {
		struct Internals;
		Internals *i;
		batched_connections(const int ip, const int port, const int max_connections);
		batched_connections(const batched_connections&) = delete;
		connection spawn();
		~batched_connections();
	};	
}
