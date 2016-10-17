#include "Socket.hpp"
#include "resource_pool.hpp"
#include "batched_connection.hpp"
#include "batched_connection_common.hpp"
#include <mutex>

namespace mutils{
	namespace batched_connection {
		
		struct receiver::connection: public ::mutils::connection {
			Socket s;
			receiver &rvr;
			const std::size_t id;
			std::size_t *bytes_sent{nullptr};
			connection(Socket s, size_t id, receiver &rvr):s(s),rvr(rvr),id(id){}
			std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const buf){
				auto ret = send_with_id(s,id,how_many,sizes,buf,total_size(how_many,sizes));
				rvr.bytes_sent += ret + (sizeof(std::size_t)*2);
				(*bytes_sent) += ret + (sizeof(std::size_t)*2);
				return ret;
			}
			bool valid() const {return true;}
			std::size_t raw_receive(std::size_t, std::size_t const * const, void **) {assert(false);}
			connection(const connection&) = delete;
		};

		receiver::receiver(int port, decltype(new_connection) new_connection)
			:port(port),
			 new_connection(new_connection),
			 acl([this](auto &a, auto b){return this->on_accept(a,b);})
		{
			//std::cout << "receiving on port: " << port << std::endl;
		}

		void receiver::loop_until_false_set(){
			//std::cout << "receiver thread up " << std::endl;
			acl.loop_until_dead(port,true);
		}

		void receiver::on_accept(bool& alive, Socket s){
			//std::cout << "beginning accept loop" << std::endl;
			while (alive) {
					//std::cout << "looping " << std::endl;
					std::size_t id;
					std::size_t size;
					s.receive(id);
					s.receive(size);
					connection conn{s,id,*this};
					bool need_new_entry = false;
					//std::cout << "received a message on connection " << id << std::endl;
					for (bool i = true; i || need_new_entry; i = false) {
						while (need_new_entry){
							//std::cout << "generating new entry" << std::endl;
							need_new_entry = false;
							std::unique_lock<std::shared_mutex> l{map_lock};
							auto &elem = receivers[id];
							elem.action = new_connection();
						}
						{
							//std::cout << "locking for receipt" << std::endl;
							//this is a "read" acquire, for a reader-writer lock
							std::shared_lock<std::shared_mutex> l{map_lock};
							if (receivers.count(id) > 0) {
								//std::cout << "receiver ready, receiving message" << std::endl;
								auto &p = receivers.at(id);
								l.unlock();
								conn.bytes_sent = &p.bytes_sent;
								//std::cout << " message sizes: ";
								//for (auto &s : p.next_expected_size)
								//std::cout << s << " ";
								//std::cout << std::endl;
								std::unique_lock<std::mutex> l{*p.mut};
								char recv_buf[size];
								s.receive(size,recv_buf);
								//std::cout << "message received" << std::endl;
								p.action(recv_buf,conn);
								//std::cout << "action performed" << std::endl;
								break;
							}
							else {
								need_new_entry = true;
							}
						}
					}
			}
		}
		
		receiver::~receiver(){
			std::cout << "bytes sent so far: " << bytes_sent << std::endl;
			for (const auto& p : receivers){
				std::cout << p.first << ":" << p.second.bytes_sent << " ";
			}
			std::cout << std::endl;
			*(acl.alive) = false;
		}
	}
}

