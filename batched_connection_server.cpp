#include "Socket.hpp"
#include "resource_pool.hpp"
#include "batched_connection.hpp"
#include "batched_connection_common.hpp"
#include "ctpl_stl.h"
#include "epoll.hpp"
#include <mutex>

namespace mutils{
	namespace batched_connection {
		
		receiver::connection::connection(Socket &s, id_type id whendebug(, std::ofstream& log_file)):s(s),id(id) whendebug(,log_file(log_file)){}

		std::size_t receiver::connection::raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const buf){
			return send_with_id(whendebug(log_file,) s,id,how_many,sizes,buf,total_size(how_many,sizes));
		}
		bool receiver::connection::valid() const {return true;}
		std::size_t receiver::connection::raw_receive(std::size_t, std::size_t const * const, void **) {
			assert(false);
			throw ProtocolException{"Cannot receive on this socket"};
		}

		receiver::receiver(int port, decltype(new_connection) new_connection)
			:receiver((std::make_index_sequence<thread_count>*) nullptr,
					  port, new_connection){}

		template<std::size_t... indices>
		receiver::receiver(std::index_sequence<indices...> const * const,
						   int port, decltype(new_connection) new_connection)
			:port(port),
			 new_connection(new_connection),
			 active_sockets{(indices ? *this : *this)...}
			 {
				 //std::cout << "receiving on port: " << port << std::endl;
			 }

		void receiver::receiver_state::receive_action(Socket &s){
			//std::cout << "looping " << std::endl;
			id_type id{0};
			size_type size{0};
			s.receive(id);
			try { 
				s.receive(size);
				assert(size > 0);
				if (receivers.size() <= id){
					receivers.resize(id + 1);
					assert(receivers[id] == nullptr);
				}
				if (!receivers[id]) {
					receivers[id] = &epoll->template add<action_items>(
						std::make_unique<action_items>(s, socket_id, id, super.new_connection),
						[](action_items &p){
							p.action->async_tick(p.conn);
						}
						);
				}
				auto &p = *receivers[id];
				whendebug(auto &log_file = p.log_file);
				//std::cout << "message received" << std::endl;
				
				char recv_buf[size];
				whendebug(auto size_rcvd = ) s.receive(size,recv_buf);
#ifndef NDEBUG
				log_file << "received " << size_rcvd << "bytes" << std::endl;
				log_file.flush();
#endif
				p.action->deliver_new_event(recv_buf,p.conn);
			}
			catch (const Timeout&){
				assert(false && "bad bad timeout detected!");
				throw ProtocolException{"timed out in the middle of a receive"};
			}
			
		}

		void receiver::receiver_thread::accept_sockets(eventfd& fd){
			fd.wait();
			std::unique_ptr<receiver_state> state;
			while (active_sockets.try_dequeue(state)){
				assert(state);
				receiver_state_set. template add<receiver_state>(
					std::move(state),
					[](receiver_state& state){
						try{
							state.tick();
						}
						catch (const Timeout&){
							//timed out on receive, try again later.
							//we know this timeout is on the first receive call,
							//so we're not going to leave the socket in an inconsistent state
						}
					}
					);
			}
		}
		
		void receiver::receiver_thread::tick_one(){
			try {
				receiver_state_set.wait();
			}
			catch(typename EPoll::epoll_removed_exception& whendebug(epe)){
				//somebody threw an exception.  Assume it was the socket;
				//don't try to fix anything just let it get cleaned up.
#ifndef NDEBUG
				try {
					std::rethrow_exception(epe.ep);
				}
				catch (std::exception &e){
					std::cerr << epe.what() << std::endl;
					std::cerr << e.what() << std::endl;
				}
				catch(...){
					std::cerr << "epoll threw a non-std exn!" << std::endl;
				}
#endif
			}
		}
		
		receiver::~receiver(){
			alive = false;
		}
	}
}

