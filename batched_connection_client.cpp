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
				std::cout << "launching with " << modulus << " physical sockets, " << max_connections << " connections" << std::endl; 
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
						s.incoming.extend(id + 1);
					}
					s.incoming[id].reset(new incoming_message_queue());
					return *s.incoming[id];
				}()){}

		locked_socket_t connection::process_data (locked_socket_t sock_lock, buf_ptr _payload, size_type payload_size)
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
				const id_type &id = ((id_type*)payload->payload)[0];
				const size_type &size = *((size_type*)(payload->payload + sizeof(id_type)));
				assert(size <= buf_size::value);
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
				std::exception_ptr payload_exn;
			};
		}

		locked_socket_t connection::from_network (locked_socket_t l,
									   size_type expected_size, buf_ptr from,
									   size_type offset)
		{
			auto into = from.grow_to_fit(expected_size + offset);
			try {
#ifndef NDEBUG
				auto into_size = into.size();
#endif
				assert(into_size > offset);
				size_type recv_size{0};
				try {
#ifndef NDEBUG
					log_file << "waiting on network" << std::endl;
					log_file.flush();
#endif
					recv_size = sock.sock.drain(into.size() - offset,into.payload + offset);
					if (recv_size == 0) {
						/*next drain should throw an exception*/
						recv_size = sock.sock.drain(into.size() - offset,into.payload + offset);
						assert(recv_size == 0);
						throw ResourceReturn{std::move(l), std::move(into),std::make_exception_ptr(ProtocolException{"Error: connection closed"})};
					}
#ifndef NDEBUG
					log_file << "network completed: received " << recv_size << " bytes (log file is " << hdr_size << " bytes)" << std::endl;
					log_file.flush();
#endif			
				}
				catch (const Timeout&){
#ifndef NDEBUG
					log_file << "network timeout; checking queue again" << std::endl;
					log_file.flush();
#endif
					throw ResourceReturn{std::move(l), std::move(into),nullptr};
				}
				assert(into_size >= recv_size);
				auto returned_l = process_data(std::move(l),std::move(into),recv_size + offset);
				assert(sock.spare.payload || sock.orphans);
				return returned_l;
			}
			catch(const ResourceReturn& ){
				std::rethrow_exception(std::current_exception());
			}
			catch(...){
				throw ResourceReturn{std::move(l), std::move(into),std::current_exception()};
			}
		};

		std::size_t connection::handle_oversized_request(std::size_t how_many, std::size_t const * const sizes, void ** bufs, std::size_t whendebug(expected_size), buf_ptr msg){
			whendebug(log_file << "oversized request detected!" << std::endl; log_file.flush());
			whendebug(log_file << "we expected: " << expected_size << " but got: " << msg.size()<< std::endl; log_file.flush());
			assert(false && "this is untested and known to be non-atomic; if a connection is interrupted while this function is running, that interrupt will result in partially-full buffers!");
			//though, we could probably just punt the problem away by having the ReadInterruptedException take a number
			//of bytes read in its constructor...
			struct dead_code{}; throw dead_code{};
			auto available_size = msg.size();
			assert(available_size > 0);
			//we don't have everything we need right now; copy what we have and keep at it.
			std::size_t how_many_available = 0;
			std::size_t final_bin_size = 0;
			{
				std::size_t used_allocation = 0;
				for (std::size_t indx = 0; indx < how_many; ++indx){
					++how_many_available;
					used_allocation += sizes[indx];
					if (used_allocation >= available_size){
						final_bin_size = sizes[indx] - (used_allocation - available_size);
						assert(final_bin_size > 0);
						break;
					}
				}
			}
			assert(final_bin_size > 0);
			std::size_t sizes_available[how_many_available];
			memcpy(sizes_available,sizes,(how_many_available - 1)* sizeof(std::size_t));
			sizes_available[how_many_available - 1] = final_bin_size;
			copy_into(how_many_available,sizes_available,bufs,(char*)msg.payload);
			
			std::size_t initial_leftover = sizes[how_many_available - 1] - sizes_available[how_many_available - 1];
			std::size_t how_many_leftover = (initial_leftover > 0 ? 1 : 0) + how_many - how_many_available;
			std::size_t sizes_leftover[how_many_leftover];
			void* bufs_leftover[how_many_leftover];
			if (initial_leftover > 0){
				sizes_leftover[0] = initial_leftover;
				bufs_leftover[0] = ((char*)bufs[how_many_available-1]) + final_bin_size;
				memcpy(sizes_leftover +1, sizes + how_many_available, sizeof(std::size_t)*(how_many_leftover -1));
				memcpy(bufs_leftover +1, bufs + how_many_available, how_many_leftover -1);
			}
			else {
				memcpy(sizes_leftover, sizes + how_many_available, sizeof(std::size_t)*how_many_leftover);
				memcpy(bufs_leftover, bufs + how_many_available, how_many_leftover);
			}
			return available_size + raw_receive(how_many_leftover,sizes_leftover,bufs_leftover);
		}

		std::size_t connection::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
			using namespace std::chrono;
			const auto expected_size = total_size(how_many, sizes);
#ifndef NDEBUG
			log_file << "processing new receive, expect total size " << expected_size << std::endl;
			log_file.flush();
#endif
			while (!*interrupted){
#ifndef NDEBUG
				log_file << "iterating new receive" << std::endl;
#endif
				if (my_queue.queue.size() > 0){
					std::unique_lock<std::mutex> l{my_queue.queue_lock};
					assert(my_queue.queue.size() > 0);
					auto msg = std::move(my_queue.queue.front());
					my_queue.queue.pop_front();
					if (msg.size() >= expected_size){
						copy_into(how_many,sizes,bufs,(char*)msg.payload);
						if (msg.size() > expected_size){
							//some message remains, put it back onto the front of this queue.
							my_queue.queue.push_front(msg.split(expected_size));
						}
					}
					else {
						l.unlock();
						return handle_oversized_request(how_many,sizes,bufs, expected_size, std::move(msg));
					}
#ifndef NDEBUG
					log_file << "found message of size " << msg.size() << " (expected " << expected_size << ") "
							 << "waiting in incoming queue" << std::endl;
					log_file.flush();
#endif
					return expected_size;
				}
				else if (auto l = ( sock.socket_lock.lock_or_abort( whendebug("network processing" ,) [&]{return *interrupted || (my_queue.queue.size() > 0);}))) {
					assert(l);
					//it would be a bad bug if somehow we had a message ready
#ifndef NDEBUG
					log_file << "lock acquired" << std::endl;
					log_file.flush();
#endif
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
							if (rr.payload_exn) std::rethrow_exception(rr.payload_exn);
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
							if (rr.payload_exn) std::rethrow_exception(rr.payload_exn);
						}
					}
				}
			}
			//only way to get here?
			//interrupted was true in condition
			*interrupted = false;
			throw ReadInterruptedException{};
		}

		
		std::size_t connection::raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs){
			whendebug(log_file << "sending (raw) " << total_size(how_many,sizes) << " bytes"; log_file.flush());
			return send_with_id(whendebug(log_file,) sock.sock,id,how_many,sizes,bufs,total_size(how_many, sizes));
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

		std::list<connection> connections::spawn(std::size_t N){
			auto &_i = i->_this;
			auto my_id = ++_i.current_connection_id;
			auto &my_socket = _i.bundles.at(my_id% _i.modulus);
			std::list<connection> ret;
			for (std::size_t cntr = 0; cntr < N; ++cntr)
			{
				id_type conn_id = my_socket->unused_id;
				++my_socket->unused_id;
				ret.emplace_back(*my_socket,conn_id);
				//send just the ID and size, resulting in initialization
				//on the server end.

				//remember that other users of this underlying socket
				//can communicate while you setup!
				ret.back().raw_send(0,nullptr,nullptr);
			}
			return ret;
		}

		connections::connections(const int ip, const int port, const int max_connections)
			:i(new Internals(ip,port,max_connections)){}
	
	}
}

