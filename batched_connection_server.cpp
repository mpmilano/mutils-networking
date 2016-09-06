#include "Socket.hpp"
#include "resource_pool.hpp"
#include "batched_connection.hpp"
#include <mutex>

namespace mutils{
	namespace batched_connection {
		
		struct receiver::connection: public ::mutils::connection {
			Socket s;
			const std::size_t id;
			connection(Socket s, size_t id):s(s),id(id){}
			std::size_t send(std::size_t expected,
							 void const * const buf){
				auto size1 = s.send(sizeof(id),&id);
				assert(size1 == sizeof(id));
				auto size2 = s.send(expected,buf);
				assert(size2 == expected);
				return expected;
			}
			bool valid() const {return true;}
			std::size_t receive(std::size_t, void*) {assert(false);}
			connection(const connection&) = delete;
		};

		receiver::receiver(int port, decltype(new_connection) new_connection)
			:port(port),
			 new_connection(new_connection),
			 acl([this](auto &a, auto b){return this->on_accept(a,b);}),
			 receiver_thread{[&]{acl.loop_until_dead(port,true);}}
		{
			std::cout << "receiving on port: " << port << std::endl;
		}

		void receiver::on_accept(bool& alive, Socket s){
			while (alive) {
				if (alive) {
					std::size_t id;
					s.receive(id);
					connection conn{s,id};
					while (true){
						bool need_new_entry = false;
						while (need_new_entry){
							need_new_entry = false;
							std::unique_lock<std::shared_mutex> l{map_lock};
							auto rcv = new_connection();
							auto &elem = receivers[id];
							elem.action = rcv.first;
							elem.next_expected_size = rcv.second;
						}
						{
							std::shared_lock<std::shared_mutex> l{map_lock};
							if (receivers.count(id) > 0) {
								auto &p = receivers.at(id);
								std::unique_lock<std::mutex> l{p.mut};
								char msg[p.next_expected_size];
								p.action(msg,conn);
								break;
							}
							else {
								need_new_entry = true;
							}
						}
					}
				}
				else break;
			}
		}
		
		receiver::~receiver(){
			*acl.alive = false;
			receiver_thread.join();
		}
	}
}

