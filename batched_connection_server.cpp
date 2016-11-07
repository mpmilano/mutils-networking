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
			:port(port),
			 new_connection(new_connection),
			 acl([this](auto &a, auto b){return this->on_accept(a,std::move(b));})
		{
			//std::cout << "receiving on port: " << port << std::endl;
		}

		void receiver::loop_until_false_set(){
			//std::cout << "receiver thread up " << std::endl;
			acl.loop_until_dead(port,true);
		}

		void receiver::on_accept(bool& server_alive, Socket s){
			//std::cout << "beginning accept loop" << std::endl;
			std::vector<action_items*> receivers;
			id_type socket_id{0};
			s.receive(socket_id);
			EPoll epoll;
			try {
				epoll. template add<Socket>(std::make_unique<Socket>(std::move(s)),[&](Socket &s){
						//std::cout << "looping " << std::endl;
						id_type id{0};
						size_type size{0};
						s.receive(id);
						s.receive(size);
						assert(size > 0);
						if (receivers.size() <= id){
							receivers.resize(id + 1);
							assert(receivers[id] == nullptr);
						}
						if (!receivers[id]) {
							receivers[id] = &epoll.template add<action_items>(
								std::make_unique<action_items>(s, socket_id, id, new_connection),
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
						
					});
				//std::cout << "action performed" << std::endl;
				while (server_alive) {
					epoll.wait();
				}
#ifndef NDEBUG
				std::cerr << "server dead; closing sockets and exiting..." << std::endl;
#endif
			}
			catch (const ProtocolException& e){
#ifndef NDEBUG
				std::cerr << "socket exception encountered (" << e.what() << "), closing socket..." << std::endl;
#endif
				//we don't really care what this error is,
				//destroy the socket and force everybody to
				//re-open on the client side.
			}
#ifndef NDEBUG
			catch (const std::exception &e){

				std::cerr << "non-socket exception encountered! (" << e.what() << "), closing socket..." << std::endl;
			}
			catch (...){
				std::cerr << "non-exception exception encountered! closing socket..." << std::endl;
			}
#endif
		}
		
		receiver::~receiver(){
			*(acl.alive) = false;
		}
	}
}

