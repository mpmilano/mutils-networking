#include "Socket.hpp"
#include "resource_pool.hpp"
#include <mutex>

namespace mutils{

	struct batched_connections_impl {
		static constexpr int connection_factor = 8;
		const int ip;
		const int port;
		const int max_connections;
		std::mutex launch_lock;
		int current_connections;
		SocketPool rp;
		Socket overflow;
		static Socket* new_resource(batched_connections_impl* _this){
			std::unique_lock<mutex> l{_this->launch_lock};
			if (_this->current_connections < _this->max_connections){
				++this->current_connections;
				return Socket::connect(ip,port);
			}
			else{
				return new Socket(_this->overflow);
			}
		}
		batched_connections_impl(const int ip, const int port, const int max_connections)
			:ip(ip),
			 port(port),
			 rp(max_connections - (max_connections/connection_factor),max_connections/connection_factor,new_resource),
			 overflow(ip,port){
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

	struct connection::Internals{
		std::shared_ptr<batched_connections_impl> parent;
	};

	connection::connection(Internals *i)
			:i(i),sock(i->parent.acquire())
			{
			}

	connection::connection(const connection& c)
		:i(new Internals(*c.i)),sock{c.sock.clone()}
	{
	}
	
	connection::~connection(){
		delete i;
	}

	batched_connections::~batched_connections(){
		delete i;
	}
	struct batched_connections::Internals{
		std::shared_ptr<batched_connections_impl>
		Internals(const int ip, const int port, const int max_connections)
			:this_sp(new batched_connections_impl(ip,port,max_connections)){}
	};

	connection batched_connections::spawn(){
		return connection{new typename connection::Internals{this_sp}};
	}

	batched_connections::batched_connections(const int ip, const int port, const int max_connections)
		:i(new Internals(ip,port,max_connections)){}
	
}

