#include "Socket.hpp"
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
		const unsigned short max_connections;
		const unsigned short modulus = (max_connections > connection_factor ?
									  max_connections / connection_factor :
									  1);
		std::atomic<id_type> current_connection_id{0};
		std::vector<std::unique_ptr<SocketBundle> > bundles{modulus};

		batched_connections_impl(const int ip, const int port, const int max_connections)
			:ip(ip),
			 port(port),
			 max_connections(max_connections)
			{
			for (auto &bundle : bundles){
				bundle.reset(new SocketBundle{
#ifndef NDEBUG
						Socket::set_timeout(
#endif
							Socket::connect(ip,port)
#ifndef NDEBUG
							,500ms)
#endif
							});
			}
		}
	};


		connection::connection(SocketBundle &s, id_type _id)
			:sock(s),id(_id),my_queue(
				[&]() -> incoming_message_queue& {
					if (s.incoming.size() <= id){
						assert(false && "we should hold a lock here, but it seems to result in deadlock");
						s.incoming.resize(id + 1);
					}
					s.incoming[id].reset(new incoming_message_queue());
					return *s.incoming[id];
				}()){}

		locked_socket_t connection::process_data (locked_socket_t sock_lock, buf_ptr _payload, size_type payload_size)
		{
			#ifndef NDEBUG
			static auto const max_vector_size =
				std::numeric_limits<unsigned char>::max()
				+ (sizeof(std::size_t)*3);
			#endif
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
				const id_type &id = ((id_type*)payload->payload)[0];
				const size_type &size = ((size_type*)payload->payload)[1];
				assert(size <= max_vector_size);
				if (payload_size < size + hdr_size) {
					assert(!sock.orphans);
					sock.orphans =
						std::unique_ptr<orphan_t>
						{new orphan_t(std::move(*payload),payload_size)};
					assert(payload_size > 0);
				}
				else {
					//payload is at least a full message; queue up that message and keep going.
					auto &queue = *sock.incoming[id];
					std::unique_lock<std::mutex> l{queue.queue_lock};
					//this is the right client.  give it the part after the header.
					queue.queue.emplace_back(payload->split(hdr_size));
					payload = &queue.queue.back();
					auto leftover = payload->split(size);
					assert(leftover.payload);
					l.unlock();
					if (payload_size > size + hdr_size){
						return process_data(std::move(sock_lock), std::move(leftover),
									 payload_size - size - hdr_size);
					}
					else {
						sock.spare = std::move(leftover);
					}
				}
			}
			return sock_lock;
		}

		namespace {
			struct ResourceReturn{
				locked_socket_t l;
				buf_ptr from;
			};
		}

		locked_socket_t connection::from_network (locked_socket_t l,
									   size_type expected_size, buf_ptr from,
									   size_type offset)
		{
			auto into = from.grow_to_fit(expected_size + offset);
#ifndef NDEBUG
			auto into_size = into.size();
#endif
			assert(into_size > offset);
			size_type recv_size{0};
			try {
				recv_size = sock.sock.drain(into.size() - offset,
											into.payload + offset);
			}
			catch (const Timeout&){
				throw ResourceReturn{std::move(l), std::move(into)};
			}
			assert(into_size >= recv_size);
			auto returned_l = process_data(std::move(l),std::move(into),recv_size + offset);
			assert(sock.spare.payload || sock.orphans);
			return returned_l;
		};

		std::size_t connection::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
			using namespace std::chrono;
			const auto expected_size = total_size(how_many, sizes);
			while (true){
				if (my_queue.queue.size() > 0){
					std::unique_lock<std::mutex> l{my_queue.queue_lock};
					assert(my_queue.queue.size() > 0);
					auto msg = std::move(my_queue.queue.front());
					my_queue.queue.pop_front();
					#ifndef NDEBUG
					if (msg.size() != expected_size){
						std::cerr << msg.size() << std::endl;
						std::cerr << expected_size << std::endl;
					}
					assert(msg.size() == expected_size);
					#endif
					copy_into(how_many,sizes,bufs,(char*)msg.payload);
					return expected_size;
				}
				else if (auto l = sock.socket_lock.lock_or_abort([&]{return (my_queue.queue.size() > 0);})) {
					assert(l);
					//it would be a bad bug if somehow we had a message ready
					assert(my_queue.queue.size() == 0);
					assert(sock.spare.payload || sock.orphans);
					const auto real_expected = expected_size + hdr_size;
					if (sock.orphans) {
						auto orphan = std::move(sock.orphans); //nulls sock.orphans
						const auto remaining_size = (orphan->size >= real_expected ?
													 orphan->size :
													 real_expected - orphan->size);
						try {
							*l = from_network(std::move(*l),
										 remaining_size,
										 std::move(orphan->buf),orphan->size);
							assert(sock.spare.payload || sock.orphans);
						}
						catch(ResourceReturn &rr){
							//we still have the lock; it's in rr
							orphan->buf = std::move(rr.from);
							sock.orphans = std::move(orphan);
							assert(sock.spare.payload || sock.orphans);
						}
					}
					else {
						assert(sock.spare.payload);
						try {
							*l = from_network(std::move(*l),
										 real_expected,
										 std::move(sock.spare),0);
							assert(sock.spare.payload || sock.orphans);
						}
						catch(ResourceReturn &rr){
							//we still have the lock; it's in rr
							sock.spare = std::move(rr.from);
							assert(sock.spare.payload || sock.orphans);
						}
					}
				}
			}
		}

		
		std::size_t connection::raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs){
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
		
		connection connections::spawn(){
			auto &_i = i->_this;
			auto my_id = ++_i.current_connection_id;
			auto &my_socket = _i.bundles.at(my_id% _i.modulus);
			id_type conn_id = my_socket->unused_id;
			++my_socket->unused_id;
			return connection{*my_socket,conn_id};
		}

		connections::connections(const int ip, const int port, const int max_connections)
			:i(new Internals(ip,port,max_connections)){}
	
	}
}

