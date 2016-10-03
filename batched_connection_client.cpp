#include "Socket.hpp"
#include "resource_pool.hpp"
#include "batched_connection.hpp"
#include "batched_connection_common.hpp"
#include <mutex>

using namespace std;
using namespace chrono;

namespace mutils{
	namespace batched_connection {

	struct batched_connections_impl {
		const int ip;
		const int port;
		const std::size_t max_connections;
		const std::size_t modulus = (max_connections > connection_factor ?
									  max_connections / connection_factor :
									  1);
		std::mutex launch_lock;
		std::atomic<std::size_t> current_connections{0};
		SocketPool rp;
		std::vector<std::unique_ptr<SocketBundle> > bundles{modulus};
		bool init_done{false};
		std::atomic<std::size_t> bytes_received{0};
		
		~batched_connections_impl(){
			std::cerr << "CLIENT BYTES RECEIVED: " << bytes_received << std::endl;
		}

		vector<size_t> abandoned_conections(const microseconds &ms){
			vector<size_t> ret;
			for (const auto &bundle : bundles){
				for (const auto& queue : bundle->incoming){
					if ((high_resolution_clock::now() - queue.second.last_used.load()) > ms)
						ret.push_back(queue.first);
				}
			}
			return ret;
		}
		
		connection* new_resource(){
			using namespace std::chrono;
			assert(!init_done);
			const std::size_t id = current_connections;
			auto index = id % (modulus);
			assert(index < bundles.size());
			auto &bundle = bundles.at(index);
			if (!bundle){
				std::unique_lock<mutex> l{launch_lock};
				if (!bundle){
					bundle.reset(new SocketBundle{Socket::connect(ip,port).set_timeout(10s),bytes_received});
				}
			}
			++current_connections;
			//don't overflow
			assert(current_connections > id);
			return new connection{*bundle,id};
		}
		batched_connections_impl(const int ip, const int port, const int max_connections)
			:ip(ip),
			 port(port),
			 max_connections(max_connections),
			 rp(modulus,max_connections - modulus,std::bind(&batched_connections_impl::new_resource,this)){
			//std::cout << "beginning pre-init" << std::endl;
			//pre-init all the connections
			std::function<void (const locked_connection&, std::size_t)> init_all;
			auto keep_for_now = std::make_unique<std::list<locked_connection> >();

			for (std::size_t ind = 0; ind < this->max_connections; ++ind){
				keep_for_now->emplace_back(this->rp.acquire());
			}
			init_done = true;
			//std::cout << "pre-init done" << std::endl;
		}
	};


		connection::connection(SocketBundle &s, std::size_t id)
			:sock(s),id(id),my_queue(&s.incoming[id]){}

		void connection::process_data (std::unique_lock<std::mutex> sock_lock, buf_ptr _payload, std::size_t payload_size)
		{
			auto* payload = &_payload;

			using orphan_t = typename SocketBundle::orphan_t;

			if (payload_size < hdr_size){
				assert(!sock.orphans);
				assert(payload_size > 0);
				sock.orphans =
					std::unique_ptr<orphan_t>
					{new orphan_t(std::move(*payload),payload_size)};
			}
			else {
				const std::size_t &id = ((std::size_t*)payload->payload)[0];
				const std::size_t &size = ((std::size_t*)payload->payload)[1];
				if (payload_size < size + hdr_size) {
					assert(!sock.orphans);
					sock.orphans =
						std::unique_ptr<orphan_t>
						{new orphan_t(std::move(*payload),payload_size)};
					assert(payload_size > 0);
				}
				else {
					{
						//payload is at least a full message; queue up that message and keep going.
						auto &queue = sock.incoming.at(id);
						std::unique_lock<std::mutex> l{queue.queue_lock};
						//this is the right client.  give it the part after the header.
						queue.queue.emplace_back(payload->split(hdr_size));
						queue.last_used = std::chrono::high_resolution_clock::now();
						payload = &queue.queue.back();
					}
					if (payload_size > size + hdr_size){
						process_data(std::move(sock_lock), payload->split(size),
									 payload_size - size - hdr_size);
					}
					else {
						sock.spare = payload->split(size);
					}
				}
			}
		}

		namespace {
			struct ResourceReturn{
				std::unique_lock<std::mutex> l;
				buf_ptr from;
			};
		}

		void connection::from_network (std::unique_lock<std::mutex> l,
									   std::size_t expected_size, buf_ptr from,
									   std::size_t offset)
		{
			auto into = from.grow_to_fit(expected_size + offset);
			assert(into.size() > offset);
			std::size_t recv_size{0};
			try {
				recv_size = sock.sock.drain(into.size() - offset,
											into.payload + offset);
				sock.bytes_received += recv_size;
			}
			catch (const Timeout&){
				throw ResourceReturn{std::move(l), std::move(into)};
			}
			process_data(std::move(l),std::move(into),recv_size + offset);
		};

		std::size_t connection::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
			const auto expected_size = total_size(how_many, sizes);
			while (true){
				if (my_queue->queue.size() > 0){
					std::unique_lock<std::mutex> l{my_queue->queue_lock};
					my_queue->last_used = std::chrono::high_resolution_clock::now();
					auto msg = std::move(my_queue->queue.front());
					my_queue->queue.pop_front();
					if (msg.size() != expected_size){
						std::cout << msg.size() << std::endl;
						std::cout << expected_size << std::endl;
					}
					assert(msg.size() == expected_size);
					copy_into(how_many,sizes,bufs,(char*)msg.payload);
					return expected_size;
				}
				else {
					std::unique_lock<std::mutex> l{sock.socket_lock};
					//retest condition - someone might have found us a message!
					//this will implicitly drop the lock.
					if (my_queue->queue.size() > 0) continue;
					assert(sock.spare.payload || sock.orphans);
					const auto real_expected = expected_size + hdr_size;
					if (sock.orphans){
						auto orphan = std::move(sock.orphans); //nulls sock.orphans
						const auto remaining_size = (orphan->size >= real_expected ?
													 orphan->size :
													 real_expected - orphan->size);
						try {
							from_network(std::move(l),
										 remaining_size,
										 std::move(orphan->buf),orphan->size);
						}
						catch(ResourceReturn &rr){
							//we still have the lock; it's in rr
							orphan->buf = std::move(rr.from);
							sock.orphans = std::move(orphan);
						}
					}
					else {
						assert(sock.spare.payload);
						try {
							from_network(std::move(l),
										 real_expected,
										 std::move(sock.spare),0);
						}
						catch(ResourceReturn &rr){
							//we still have the lock; it's in rr
							sock.spare = std::move(rr.from);
						}
					}
				}
			}
		}
		
		std::size_t connection::raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs){
			my_queue->last_used = std::chrono::high_resolution_clock::now();
			return send_with_id(sock.sock,id,how_many,sizes,bufs,total_size(how_many, sizes));
		}

		struct connections::Internals{
			batched_connections_impl _this;
			Internals(const int ip, const int port, const int max_connections)
				:_this(ip,port,max_connections){}
		};
		
		connections::~connections(){
			delete i;
		}
		
		locked_connection connections::spawn(){
			return i->_this.rp.acquire();
		}

		std::vector<std::size_t> connections::abandoned_conections(const std::chrono::microseconds &d, bool){
			return i->_this.abandoned_conections(d);
		}

		connections::connections(const int ip, const int port, const int max_connections)
			:i(new Internals(ip,port,max_connections)){}
	
	}
}

