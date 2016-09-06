#pragma once
#include "Socket.hpp"
#include "resource_pool.hpp"
#include "better_cv.hpp"
#include <shared_mutex>
#include "ServerSocket.hpp"

namespace mutils{
	namespace batched_connection {

		struct batched_connections_impl;

		struct SocketBundle{
			Socket sock;
			condition_variable cv;
			SocketBundle(Socket sock):sock(sock){}
			SocketBundle(const SocketBundle&) = delete;
			SocketBundle(SocketBundle&&) = delete;
		};
		
		struct connection : public ::mutils::connection {
			SocketBundle& sock;
			const std::size_t id;
			connection(SocketBundle& s, std::size_t id);
			operator bool() const {return valid();}
			connection(const connection&) = delete;
			connection(connection&&) = default;
			std::size_t receive(std::size_t expected, void * parent_buf);
			std::size_t send(std::size_t expected, void const * const buf);
			bool valid () const {return sock.sock.valid();}
		};
		
		using SocketPool = ResourcePool<connection>;
		using locked_connection = typename SocketPool::LockedResource;
		using weak_connection = typename SocketPool::WeakResource;
		
		
		struct batched_connections {
			struct Internals;
			Internals *i;
			batched_connections(const int ip, const int port, const int max_connections);
			batched_connections(const batched_connections&) = delete;
			locked_connection spawn();
			~batched_connections();
		};

		struct receiver {
			//returns expected next message size
			struct connection;
			const int port;
			using action_t =
				std::function<std::size_t (char*, ::mutils::connection&)>;
			
			struct action_items{
				action_t action;
				std::size_t next_expected_size;
				std::mutex mut;
				action_items() = default;
				action_items(std::pair<action_t, std::size_t> a)
					:action(a.first),next_expected_size(a.second){}
				action_items(action_items&&) = default;
			};
			std::function<std::pair<action_t, std::size_t> ()> new_connection;

			std::shared_mutex map_lock;
			std::map<std::size_t,  action_items> receivers;
			AcceptConnectionLoop acl;
			std::thread receiver_thread;

			void on_accept(bool& alive, Socket s);

			receiver(int port, decltype(new_connection) new_connection);
			~receiver();
		};
	}
}
