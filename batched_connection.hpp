#pragma once
#include "Socket.hpp"
#include "resource_pool.hpp"
#include "better_cv.hpp"
#include <shared_mutex>
#include "ServerSocket.hpp"
#include "buffer_generator.hpp"
#include "readerwriterqueue.h"
#include "better_constructable_array.hpp"
#include "epoll.hpp"

namespace mutils {
	namespace batched_connection {

		struct batched_connections_impl;
		using BufGen = BufferGenerator<4096*8*8>;
		using buf_ptr = BufGen::pointer;
		static const constexpr std::size_t hdr_size = 2*sizeof(std::size_t);
		
		struct incoming_message_queue{
			std::list<buf_ptr> queue;
			std::mutex queue_lock;
		};

		/** Maintains the message queues for all logical connections
		 *  associated with a physical TCP socket
		 */
		struct SocketBundle {
			Socket sock;
			std::map<std::size_t,incoming_message_queue> incoming;
			std::mutex socket_lock;;

			//represents a partial message whose
			//destination is not currently known
			//(we don't know if we've only gotten a prefix of the id)
			struct orphan_t{
				buf_ptr buf;
				std::size_t size;
				
				orphan_t(buf_ptr b, std::size_t s)
					:buf(std::move(b)),size(s){}
			};

			//if there was an orphan on the last read, it is here.
			std::unique_ptr<orphan_t> orphans{nullptr};

			//this is some leftover free space;
			//treat it like a freshly allocated buffer.
			//invariant: not in use, of non-zero size.
			buf_ptr spare{BufGen::allocate()};
			
			SocketBundle(Socket sock)
				:sock(sock){}
			SocketBundle(const SocketBundle&) = delete;
			SocketBundle(SocketBundle&&) = delete;
		};
		
		struct connection : public ::mutils::connection {
			SocketBundle& sock;
			
			const std::size_t id;
			incoming_message_queue &my_queue;
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

		/** Number of logical connections per physical connection. */
		static const constexpr std::size_t connection_factor = 1;
		
		/** Manages a set of logical TCP connections over a smaller set of physical TCP
		 *  connections.
		 */
		struct connections {
			struct Internals;
			static_assert(sizeof(int)*2 <= sizeof(std::size_t),"Error: assumed two ints fit in size_t");
			Internals *i;
			
			//connect to a server at address IP and port PORT, and open max_connections logical connections.
			connections(const int ip, const int port, const int max_connections);
			
			connections(const connections&) = delete;
			
			connection spawn();
			~connections();
		};

		//Manages a physical TCP socket (to accept on) on behalf
		//of its clients.  Is intended to be an RPC mechanism;
		//the user passes in a "new_connection" function,
		//which must return action functions.  On receipt of a message,
		//the action funciton is called.  There is a unique action function
		//(produced by a separate call to new_connection) per logical connection.

		struct ReceiverFun {
			virtual void operator()(const void*, ::mutils::connection&) = 0;
			virtual ~ReceiverFun(){}
		};
		
		struct receiver {
			
			//returns expected next message size
			struct connection;
			//function to call when new messages come in.
			using action_t = std::unique_ptr<ReceiverFun>;

			//Represents  a logical connection; consider it an
			//"instance" of the action that this receiver is set up to
			//perform.  
			struct action_items {
				action_t action;
				action_items() = default;
				action_items(action_t a)
					:action(std::move(a)){}
			};

			struct receiver_state{
				std::map<std::size_t,  std::unique_ptr<action_items> > receivers;
				Socket s;
				receiver &super;
				void tick();
				receiver_state(Socket s, receiver &super);
				int underlying_fd() {return s.underlying_fd();}
				receiver_state& operator=(receiver_state&&);
				receiver_state(receiver_state&& o);
			};

			using new_connection_t = std::function<action_t () >;
			
			new_connection_t new_connection;

			const int port;
			bool alive{true};

			constexpr static unsigned char thread_count = 16;
			struct receiver_thread{
				receiver &super;
				EPoll<receiver_state> epoll_obj;
				receiver_thread(receiver& super)
					:super(super){}
				moodycamel::ReaderWriterQueue<std::unique_ptr<receiver_state> > active_sockets;
				void tick_one();
				std::thread this_thread{
					[&]{while (super.alive)
							tick_one();
					}
				};
			};
				
			array<receiver_thread, thread_count, receiver&> active_sockets;
			void acceptor_fun();

			receiver(int port, new_connection_t new_connection);
			template<std::size_t... indices>
			receiver(std::index_sequence<indices...> const * const,
					 int port, new_connection_t new_connection);
			~receiver();
		};
	}
}
