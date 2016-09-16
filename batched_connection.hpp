#pragma once
#include "Socket.hpp"
#include "resource_pool.hpp"
#include "better_cv.hpp"
#include <shared_mutex>
#include "ServerSocket.hpp"

namespace mutils{
	namespace batched_connection {

		struct batched_connections_impl;
		using BufGen = BufferGenerator<4096>;
		using buf_ptr = struct BufGen::pointer;
		static const constexpr std::size_t hdr_size = 2*sizeof(std::size_t);
		
		struct incoming_message_queue{
			std::list<buf_ptr> queue;
			std::shared_mutex queue_lock;
		};
		
		struct SocketBundle{
			Socket sock;
			std::map<std::size_t,incoming_message_queue> incoming;
			std::mutex socket_lock;

			std::unique_ptr<buf_ptr> orphans;
			std::size_t orphan_size{0};
			buf_ptr spare{BufGen::allocate()};
			
			SocketBundle(Socket sock):sock(sock){}
			SocketBundle(const SocketBundle&) = delete;
			SocketBundle(SocketBundle&&) = delete;
		};
		
		struct connection : public ::mutils::connection {
			SocketBundle& sock;
			const std::size_t id;
			incoming_message_queue& my_queue;
			//use this if there's nothing left over.
			connection(SocketBundle& s, std::size_t id);
			operator bool() const {return valid();}
			connection(const connection&) = delete;
			connection(connection&&) = default;
		private:
			buf_ptr process_data(std::size_t id, std::size_t size, buf_ptr _payload, std::size_t payload_size);
			buf_ptr from_network (buf_ptr into);
		public:
			std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs);
			std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const);
			bool valid () const {return sock.sock.valid();}
			
			template<typename... T> auto receive(T&& ... t){
				::mutils::connection& _this = *this;
				return _this.receive(std::forward<T>(t)...);
			}
			
			template<typename... T> auto send(T&& ... t){
				::mutils::connection& _this = *this;
				return _this.send(std::forward<T>(t)...);
			}
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
				std::function<std::vector<std::size_t> (void**, ::mutils::connection&)>;
			
			struct action_items{
				action_t action;
				std::vector<std::size_t> next_expected_size;
				std::mutex mut;
				action_items() = default;
				action_items(std::pair<action_t, std::size_t> a)
					:action(a.first),next_expected_size(a.second){}
				action_items(action_items&&) = default;
			};
			std::function<std::pair<action_t,
									std::vector<std::size_t> > ()> new_connection;

			std::shared_mutex map_lock;
			std::map<std::size_t,  action_items> receivers;
			AcceptConnectionLoop acl;

			void on_accept(bool& alive, Socket s);

			void loop_until_false_set();

			receiver(int port, decltype(new_connection) new_connection);
			~receiver();
		};
	}
}
