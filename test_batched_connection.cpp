#include "batched_connection.hpp"
#include "simple_rpc.hpp"
#include "GlobalPool.hpp"
#include <unistd.h>

using namespace mutils;
using namespace std;
namespace conn_space = mutils::simple_rpc;


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
	using action_t = typename conn_space::receiver::action_t;
	std::thread receiver{[&]{
			conn_space::receiver{portno,[]{
					//std::cout << "receiver triggered" << std::endl;
					return action_t{
						[on_first_message = std::make_shared<bool>(true),
						 assert_single_threaded = std::make_shared<std::mutex>()]
							(const void* inbnd, connection& c) -> void{
							bool success = assert_single_threaded->try_lock();
							assert(success);
							AtScopeEnd ase{[&]{assert_single_threaded->unlock();}};
							//std::cout << "received message" << std::endl;
							if (*on_first_message){
								//std::cout << "expected this message is size_t" << std::endl;
								int rcv = *((int*)(inbnd));
								c.send(rcv);
								*on_first_message = false;
							}
							else{
								//std::cout << "expected this message is vector" << std::endl;
								auto received = from_bytes<std::vector<unsigned char> >(nullptr,((char*) inbnd));
								{
									stringstream ss;
									ss << "received " << received->size() << " element vector" << std::endl;
									//std::cout << ss.str();
								}
								auto random = better_rand();
								assert(random >= 0 && random <= 1);
								uint index = random * received->size();
								if (index == received->size()) index = 0;
								if (received->size() != 0){
									assert(index < received->size());
									(*received)[index]++;
								}
								{
									stringstream ss;
									ss << "sending " << c.send(*received) << " bytes of vector back" << std::endl;
									//std::cout << ss.str();
								}
								*on_first_message = true;
							}
						}};
				}}.loop_until_false_set();
		}};
	sleep(1);
	conn_space::connections bc(decode_ip("127.0.0.1"),portno,MAX_THREADS/2);
	using connection = conn_space::connection;

	std::map<int, connection> connections;
	struct debug_info{
		int my_msg;
		int receipt;
	};
	using namespace std::chrono;
	using time_t = decltype(high_resolution_clock::now());
	using duration_t =
		std::decay_t<decltype(std::declval<time_t>() - std::declval<time_t>() )>;
	unsigned long long total_events{0};
	unsigned long long outlier10_count{0};
	unsigned long long outlier100_count{0};
	//std::map<int, volatile time_t > event_counts;
	//for (int index = 0; index < MAX_THREADS/2; ++index) event_counts.emplace(index,  high_resolution_clock::now());
	duration_t total_time{0};
	try {
		//randomly generate lenghts,
		//also maybe don't echo, increment? 
		for(int index = 0; index < MAX_THREADS/2; ++index){
			auto my_msg = index;
			if (my_msg %50 == 0) std::cout << "on message " << my_msg << std::endl;
			connections.emplace(my_msg,bc.spawn());
			auto *c = &connections.at(my_msg);
			GlobalPool::inst.push([c,
								   &total_time,
								   &total_events,
								   &outlier10_count,
								   &outlier100_count,
								   my_msg](int) mutable
								  {
					for(unsigned int i = 0; true; ++i){
						/*time_t old_time = event_counts.at(my_msg);
						auto now = high_resolution_clock::now();
						assert(now - old_time < 30s);
						event_counts.at(my_msg) = now;//*/
						auto average = total_time / (total_events + 1.0);
						//auto outlier10_average = outlier10_count / (total_events + 1.0);
						//auto outlier100_average = outlier100_count / (total_events + 1.0);
						auto start = high_resolution_clock::now();
						int receipt{-1};
						c->send(my_msg);
						//std::cout << "sending initial message" << std::endl;
						c->receive(receipt);
						//std::cout << "received initial message reply" << std::endl;

						auto end = high_resolution_clock::now();
						{
							std::vector<unsigned char> my_other_message;
							const std::size_t other_message_size = (better_rand()*50) + 1;
							assert(other_message_size > 0);
							assert(other_message_size < 51);
							const unsigned char max_uchar = std::numeric_limits<unsigned char>::max();
							for (std::size_t i = 0; i < other_message_size; ++i){
								my_other_message.push_back(better_rand() * max_uchar);
							}
							//std::cout << "sending my other message" << std::endl;
							auto wire_size = c->send(my_other_message);
							{
								stringstream ss;
								ss << "sent " << my_other_message.size() << " element vector" << std::endl;
								//std::cout << ss.str();
							}
							::mutils::connection& c_super = *c;
							{
								stringstream ss;
								ss << "expecting " << wire_size << "bytes of vector in reply" << std::endl;
								//cout << ss.str();
							}
							auto received_other_message = c_super.receive<std::vector<unsigned char> >
								((DeserializationManager*)nullptr, wire_size);
							//std::cout << "received my other message reply" << std::endl;
							bool has_been_off = false;
							assert(my_other_message.size() == received_other_message->size());
							for (std::size_t i = 0; i < my_other_message.size(); ++i){
								bool check = (my_other_message.at(i) ==
											  received_other_message->at(i)
											  || (has_been_off ? true :
												  (has_been_off = true,
												   (my_other_message.at(i) + 1) ==
												   received_other_message->at(i))));
								if  (!check){
									std::cerr << my_other_message << std::endl;
									std::cerr << *received_other_message << std::endl;
								}
								assert(check);
							}
							assert(has_been_off || (my_other_message.size() == 0));
						}
						auto duration = (end - start);
						if (i % 20000 == 0) {
							if (total_events > 0) std::cout << duration_cast<microseconds>(average).count() << std::endl;
							//std::cout << outlier100_average << std::endl;
							/*
							int num_behind = 0;
							for (const auto &indx : event_counts){
								if ((start - indx.second.load()) > 10s){
									++num_behind;
								}
							}
							if (num_behind > 0) std::cout << num_behind << std::endl;//*/
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
