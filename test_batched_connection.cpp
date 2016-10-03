#include "batched_connection.hpp"
#include "GlobalPool.hpp"
#include <unistd.h>

using namespace mutils;
using namespace std;

int main(int argc, char* argv[]){

	{
		constexpr int port = 9845;
		int recv_port{0};
		ServerSocket ss{port};
		std::thread t{[&]{
				auto s = ss.receive();
				s.receive(recv_port);
				assert(recv_port == port);
				int recv{0};
				s.receive(recv);
				assert(recv == recv_port);
				recv = 0;
				auto amount = s.drain(1000,&recv);
				assert(amount == sizeof(recv));
				assert(recv == recv_port);
			}};
		Socket s = Socket::connect(decode_ip("127.0.0.1"),port);
		s.send(port);
		s.send(port,port);
		t.join();
	}
	
	const auto portno = (argc > 1 ? std::atoi(argv[1]) : 9876);
	std::cout << "portno is: " << portno << std::endl;
	using action_t = typename batched_connection::receiver::action_t;
	std::thread receiver{[&]{
			batched_connection::receiver{portno,[]{
					return action_t{
						[](void* inbnd, connection& c) -> void{
							int rcv = *((int*)(inbnd));
							c.send(rcv);
						}};
				}}.loop_until_false_set();
		}};
	sleep(1);
	batched_connection::batched_connections bc(decode_ip("127.0.0.1"),portno,MAX_THREADS/2);
	using connection = batched_connection::locked_connection;

	std::function<void (const connection&) > looper;
	std::map<int, connection> connections;
	struct debug_info{
		int my_msg;
		int receipt;
	};
	using namespace std::chrono;
	using time_t = decltype(high_resolution_clock::now());
	using duration_t =
		std::decay_t<decltype(high_resolution_clock::now() -
							  high_resolution_clock::now())>;
	unsigned long long total_events{0};
	unsigned long long outlier10_count{0};
	unsigned long long outlier100_count{0};
	std::map<int, std::atomic<time_t> > event_counts;
	for (int index = 0; index < MAX_THREADS/2; ++index) event_counts[index] = high_resolution_clock::now();
	duration_t total_time{0};
	try {
		//randomly generate lenghts,
		//also maybe don't echo, increment? 
		for(int index = 0; index < MAX_THREADS/2; ++index){
			auto my_msg = index;
			if (my_msg %50 == 0) std::cout << "on message " << my_msg << std::endl;
			connections.emplace(my_msg,bc.spawn());
			auto &c = connections.at(my_msg);
			GlobalPool::inst.push([&c,
								   &total_time,
								   &event_counts,
								   &total_events,
								   &outlier10_count,
								   &outlier100_count,
								   my_msg](int) mutable
								  {
					for(int i = 0; true; ++i){
						event_counts.at(my_msg) = high_resolution_clock::now();
						auto average = total_time / (total_events + 1.0);
						//auto outlier10_average = outlier10_count / (total_events + 1.0);
						//auto outlier100_average = outlier100_count / (total_events + 1.0);
						auto start = high_resolution_clock::now();
						c->send(my_msg);
						int receipt{-1};
						c->receive(receipt);
						auto end = high_resolution_clock::now();
						auto duration = (end - start);
						int num_behind = 0;
						if (i % 500 == 0) {
							//if (total_events > 0) std::cout << duration_cast<microseconds>(average).count() << std::endl;
							//std::cout << outlier100_average << std::endl;
							for (const auto &indx : event_counts){
								if ((start - indx.second.load()) > 6s){
									++num_behind;
								}
							}
							if (num_behind > 0) std::cout << num_behind << std::endl;
						}
						if (i > 10000){
							if (duration > 10*average) ++outlier10_count;
							if (duration > 100*average) ++outlier100_count;
							total_time += duration;
							++total_events;
						}
						assert(receipt == my_msg);
						if (receipt != my_msg){
							assert(false);
							throw debug_info{my_msg,receipt};
						}
						
					}
				});
		}
	}
	catch (const debug_info& exn){
		std::cerr << "message error: " << exn.my_msg << " : " << exn.receipt << std::endl;
	}
	receiver.join();
	sleep(5000);
}
