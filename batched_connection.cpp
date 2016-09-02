#include "Socket.hpp"
#include "resource_pool.hpp"
#include <mutex>

namespace mutils{
	namespace batched_connection {

	struct batched_connections_impl {
		static constexpr int connection_factor = 8;
		const int ip;
		const int port;
		const int max_connections;
		std::mutex launch_lock;
		int current_connections;
		SocketPool rp;
		SocketBundle overflow;
		static Socket* new_resource(batched_connections_impl* _this){
			std::unique_lock<mutex> l{_this->launch_lock};
			if (_this->current_connections < _this->max_connections){
				++this->current_connections;
				return SocketBundle{Socket::connect(ip,port)};
			}
			else{
				return new SocketBundle(_this->overflow);
			}
		}
		batched_connections_impl(const int ip, const int port, const int max_connections)
			:ip(ip),
			 port(port),
			 rp(max_connections - (max_connections/connection_factor),max_connections/connection_factor,new_resource),
			 overflow(Socket{ip,port}){
			//pre-init all the connections
			std::function<void (const LockedResource&, int)> init_all;
			init_all = [&](const LockedResource&, int ind){
				if (ind < this->max_connections) {
					init_all(this->rp.acquire(this),ind+1);
				}
			};
		}
	};
	batched_connections_impl::connection_factor;


	connection::connection(batched_connections_impl &parent)
			:sock(parent.acquire())
			{
			}

	connection::connection(const connection& c)
		:sock{c.sock.clone()}
	{
	}

		std::size_t connection::recv(std::size_t expected, char * parent_buf){
			auto bundle = sock.lock();
			return [&](const auto &){
				{
					std::size_t buf;
					bundle.sock.recv(sizeof(buf),&buf);
				}
				bool worked = expected ==
					bundle.sock.recv(expected,parent_buf);
				assert(worked);
				return expected;
			}(bundle.cv.wait([&]{
						std::size_t buf;
						bundle.sock.peek(sizeof(buf),&buf);
						return buf == id;
					}));
		}
		
		std::size_t connection::send(std::size_t expected, char const * const buf){
			auto bundle = sock.lock();
			return [&](const auto &){
				bundle.sock.send(sizeof(id),&id);
				bool worked = expected ==
					bundle.sock.send(expected,buf);
				assert(worked);
				return expected;
			}(bundle.cv.wait([]{true})));
		}

	batched_connections::~batched_connections(){
		delete i;
	}
	struct batched_connections::Internals{
		batched_connections_impl _this;
		Internals(const int ip, const int port, const int max_connections)
			:_this(ip,port,max_connections){}
	};

	connection batched_connections::spawn(){
		return connection{_this};
	}

	batched_connections::batched_connections(const int ip, const int port, const int max_connections)
		:i(new Internals(ip,port,max_connections)){}
	
	}
}

