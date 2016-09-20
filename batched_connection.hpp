#pragma once
#include "Socket.hpp"
#include "resource_pool.hpp"
#include "better_cv.hpp"
#include <shared_mutex>
#include "ServerSocket.hpp"
#include "buffer_generator.hpp"

namespace mutils{
	namespace batched_connection {

		struct batched_connections_impl;
		using BufGen = BufferGenerator<4096>;
		using buf_ptr = struct BufGen::pointer;
		static const constexpr std::size_t hdr_size = 2*sizeof(std::size_t);
		
		struct incoming_message_queue{
			std::list<buf_ptr> queue;
			std::mutex queue_lock;
		};
		
		struct SocketBundle{
			Socket sock;
			std::map<std::size_t,incoming_message_queue> incoming;
			std::mutex socket_lock;

			struct orphan_t{
				buf_ptr buf;
				std::size_t size;
				
				orphan_t(buf_ptr b, std::size_t s)
					:buf(std::move(b)),size(s){}
			};
			std::unique_ptr<orphan_t> orphans{nullptr};
			buf_ptr spare{BufGen::allocate()};
			
			SocketBundle(Socket sock):sock(sock){}
			SocketBundle(const SocketBundle&) = delete;
			SocketBundle(SocketBundle&&) = delete;
		};
		
		struct connection : public ::mutils::connection {
			SocketBundle& sock;
			const std::size_t id;
			void* bonus_item{nullptr};
			incoming_message_queue& my_queue;
			//use this if there's nothing left over.
			connection(SocketBundle& s, std::size_t id);
			operator bool() const {return valid();}
			connection(const connection&) = delete;
			connection(connection&&) = default;
		private:
			void process_data (std::unique_lock<std::mutex> sock_lock, buf_ptr _payload, std::size_t payload_size);
			void from_network (std::unique_lock<std::mutex> l,
							   std::size_t expected_size, buf_ptr from,
							   std::size_t offset);
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
				std::function<void (void*, ::mutils::connection&)>;
			
			struct action_items{
				action_t action;
				std::unique_ptr<std::mutex> mut{new std::mutex()};
				action_items() = default;
				action_items(action_t a)
					:action(std::move(a)){}
				action_items(action_items&& o)
					:action(std::move(o.action)),
					 mut(std::move(o.mut)){}
			};
			std::function<action_t () > new_connection;

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
