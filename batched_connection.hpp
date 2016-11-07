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
#include "better_constructable_array.hpp"
#include "eventfd.hpp"

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
		
		struct connection : public ::mutils::connection {
			SocketBundle& sock;
			
			const id_type id;
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
		
		/** Manages a set of logical TCP connections over a smaller set of physical TCP
		 *  connections.
		 */
		struct connections {
			struct Internals;
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
			//this *must* be wait-free.  We're calling it in the receive thread!
			virtual void deliver_new_event(const void*, ::mutils::connection&) = 0;
			//this *must* be wait-free.  We're calling it in the receive thread!
			virtual void async_tick(::mutils::connection&) = 0;
			//must be able to select() on this int as an FD
			//where a "read" ready indicates it's time to
			//call async_tick
			virtual int underlying_fd() = 0;
			virtual ~ReceiverFun(){}
		};
		
		struct receiver {
			
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
			using action_t = std::unique_ptr<ReceiverFun>;

			using new_connection_t = std::function<action_t (whendebug(std::ofstream&)) >;
			new_connection_t new_connection;

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
					:socket_id(sid),id(id),conn(s, id whendebug(, log_file)),action(nc(whendebug(log_file))){}
			};

			struct receiver_state{
				std::unique_ptr<EPoll> epoll{new EPoll()};
				std::vector<action_items*> receivers;
				id_type socket_id{0};
				receiver &super;
				void tick(){
					epoll->wait();}
				void receive_action(Socket &s);
				receiver_state(Socket s, receiver &super)
					:super(super){
					s.receive(socket_id);
					epoll-> template add<Socket>(
						std::make_unique<Socket>(Socket::set_timeout(std::move(s),std::chrono::milliseconds(5))),
						[&](Socket &s){return receive_action(s);});
				}
				
				int underlying_fd() {return epoll->underlying_fd();}
				receiver_state& operator=(receiver_state&& o){
					assert(&super == &o.super);
					epoll = std::move(o.epoll);
					receivers = std::move(o.receivers);
					return *this;
				}
				receiver_state(receiver_state&& o)
					:epoll(std::move(o.epoll)),
					 receivers(std::move(o.receivers)),
					 super(o.super){}
			};

			constexpr static unsigned char thread_count = 16;

			bool alive{true};
			
			struct receiver_thread{
				receiver &super;
				EPoll receiver_state_set;
				receiver_thread(receiver& super)
					:super(super),
					 active_sockets_notify(
						 receiver_state_set.template add<eventfd>(
							 std::make_unique<eventfd>(),
							 [&] (eventfd& fd){return accept_sockets(fd);} ))
					{}
				moodycamel::ReaderWriterQueue<std::unique_ptr<receiver_state> > active_sockets;
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

			void on_accept(bool& alive, Socket s);

			void acceptor_fun(){
				using namespace std::chrono;
				ServerSocket ss{port};
				unsigned char last_used_list{0};
				while (alive){
					auto next = (last_used_list + 1)%thread_count;
					
					active_sockets[next].active_sockets.enqueue(
						std::make_unique<receiver_state>(ss.receive(),*this));
					active_sockets[next].active_sockets_notify.notify();
					last_used_list = next;
				}
			}

			receiver(int port, new_connection_t new_connection);
			template<std::size_t... indices>
			receiver(std::index_sequence<indices...> const * const,
					 int port, new_connection_t new_connection);
			~receiver();
		};
	}
}
