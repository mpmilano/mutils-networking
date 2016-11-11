#pragma once
#include "Socket.hpp"
#include <mutex>
#include <fstream>
#include <sstream>
#include "abortable_lock.hpp"
#include <atomic>
#include "ServerSocket.hpp"
#include "buffer_generator.hpp"
#include "epoll.hpp"
#include "readerwriterqueue.h"
#include "interruptible_connection.hpp"
#include "better_constructable_array.hpp"
#include "eventfd.hpp"
#include "rpc_api.hpp"

namespace mutils {
	namespace batched_connection {

		struct batched_connections_impl;
		using id_type = unsigned short;
		using size_type = unsigned short;
		using BufGen = BufferGenerator<4096*8*8>;
		using buf_ptr = BufGen::pointer;
		static const constexpr std::size_t hdr_size = sizeof(id_type) + sizeof(size_type);

		/** Number of logical connections per physical connection. */
		static const constexpr unsigned short connection_factor = 8;
		
		struct incoming_message_queue{
			std::list<buf_ptr> queue;
			std::mutex queue_lock;
		};

		using locked_socket_t = std::unique_lock<typename abortable_locked_guardian::abortable_mutex>;

		/** Maintains the message queues for all logical connections
		 *  associated with a physical TCP socket
		 */
		struct SocketBundle {
			Socket sock;
			const id_type socket_id = gensym();
			std::atomic<id_type> unused_id{0};
			std::vector<std::unique_ptr<incoming_message_queue> > incoming{connection_factor*2};
			abortable_locked_guardian socket_lock;
			
			//represents a partial message whose
			//destination is not cubrrently known
			//(we don't know if we've only gotten a prefix of the id)
			struct orphan_t{
				buf_ptr buf;
				size_type size;
				
				orphan_t(buf_ptr b, size_type s)
					:buf(std::move(b)),size(s){}
			};

			//if there was an orphan on the last read, it is here.
			std::unique_ptr<orphan_t> orphans{nullptr};

			//this is some leftover free space;
			//treat it like a freshly allocated buffer.
			//invariant: not in use, of non-zero size.
			buf_ptr spare{BufGen::allocate()};
			
			SocketBundle(Socket _sock)
				:sock(std::move(_sock)){
				sock.send(socket_id);
			}
			SocketBundle(const SocketBundle&) = delete;
			SocketBundle(SocketBundle&&) = delete;
		};
		
		struct connection : public interruptible_connection {
			SocketBundle& sock;
			
			const id_type id;
			bool interrupted{false};
#ifndef NDEBUG
			std::ofstream log_file{[&](){
					std::stringstream ss;
					ss << "/tmp/event_log_client"
					   << sock.socket_id
					   << "-"
					   << id;
					return ss.str(); }()};
#endif
			incoming_message_queue &my_queue;

			connection(SocketBundle& s, id_type id);
			operator bool() const {return valid();}
			connection(const connection&) = delete;
			connection(connection&&) = default;
		private:
			locked_socket_t process_data (locked_socket_t sock_lock, buf_ptr _payload, size_type payload_size);
			locked_socket_t from_network (locked_socket_t l,
										  size_type expected_size, buf_ptr from,
										  size_type offset);
			std::size_t handle_oversized_request(std::size_t how_many,
												 std::size_t const * const sizes,
												 void ** bufs,
												 buf_ptr msg);
		public:
			std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs);
			std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const);
			bool valid () const {return sock.sock.valid();}
			void interrupt() {interrupted = true;}
			void clear_interrupt() {interrupted = false;}
			
			template<typename... T> auto receive(T&& ... t){
				::mutils::connection& _this = *this;
				return _this.receive(std::forward<T>(t)...);
			}
			
			template<typename... T> auto send(T&& ... t){
				::mutils::connection& _this = *this;
				return _this.send(std::forward<T>(t)...);
			}
		};
		
		/** Manages a set of logical TCP connections over a smaller set of physical TCP
		 *  connections.
		 */
		struct connections {
			struct Internals;
			Internals *i;
			
			//connect to a server at address IP and port PORT, and open max_connections logical connections.
			connections(const int ip, const int port, const int max_connections);
			
			connections(const connections&) = delete;

			//spawns N connections over the same physical link.
			std::list<connection> spawn(std::size_t N);
			connection spawn();
			~connections();
		};

		//Manages a physical TCP socket (to accept on) on behalf
		//of its clients.  Is intended to be an RPC mechanism;
		//the user passes in a "new_connection" function,
		//which must return action functions.  On receipt of a message,
		//the action funciton is called.  There is a unique action function
		//(produced by a separate call to new_connection) per logical connection.
		
		using rpc::ReceiverFun;
		
		struct receiver {

			using new_connection_t = rpc::new_connection_t;
			using action_t = rpc::action_t;
			
			//returns expected next message size
			struct connection : public ::mutils::connection {
				Socket &s;
				const id_type id;
				whendebug(std::ofstream &log_file;)
				connection(Socket &s, id_type id whendebug(, std::ofstream& log_file));
				connection(connection&& c) = default;
				std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const buf);
				bool valid() const;
				std::size_t raw_receive(std::size_t, std::size_t const * const, void **);
				connection(const connection&) = delete;
			};
			// physical TCP port
			const int port;
			//function to call when new messages come in.
			rpc::new_connection_t new_connection;

			//Represents  a logical connection; consider it an
			//"instance" of the action that this receiver is set up to
			//perform.  
			struct action_items {
				const id_type socket_id;
				const id_type id;
#ifndef NDEBUG
				std::ofstream log_file{[&](){
						std::stringstream ss;
						ss << "/tmp/event_log_server"
						   << socket_id
						   << "-"
						   << id;
						return ss.str(); }()};
#endif
				connection conn;
				action_t action;
				auto underlying_fd() const {return action->underlying_fd();}
				std::mutex mut;
				action_items() = default;
				action_items(Socket &s, const id_type sid, const id_type id, new_connection_t& nc)
					:socket_id(sid),id(id),conn(s, id whendebug(, log_file)),action(nc(whendebug(log_file,) conn)){}
			};

			struct receiver_state{
				Socket s;
				std::vector<action_items*> receivers;
				const id_type socket_id;
				receiver &super;
				int underlying_fd() {return s.underlying_fd();}
				void receive_action(EPoll&);
				receiver_state(Socket s, id_type sid, receiver &super);
			};

			constexpr static unsigned char thread_count = 16;

			bool alive{true};
			
			struct receiver_thread{
				receiver &super;
				EPoll receiver_state_set;
				receiver_thread(receiver& super);

				void register_receiver_state(Socket s);
				
				moodycamel::ReaderWriterQueue<std::unique_ptr<Socket> > active_sockets;
				eventfd& active_sockets_notify;
				void accept_sockets(eventfd& fd);
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
