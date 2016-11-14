#include "batched_connection.hpp"
#include "simple_rpc.hpp"
#include "GlobalPool.hpp"
#include <unistd.h>

using namespace mutils;
using namespace std;
namespace conn_space = mutils::batched_connection;


int main(int argc, char* argv[]){
	const auto portno = (argc > 1 ? std::atoi(argv[1]) : 9876);
	std::cout << "portno is: " << portno << std::endl;
	sleep(1);
	auto bc = std::make_unique<conn_space::connections>(decode_ip("127.0.0.1"),portno,MAX_THREADS/2);
	
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
	std::list<std::future<bool> > running_threads;
	try {
		//randomly generate lenghts,
		//also maybe don't echo, increment? 
		for(int index = 0; index < MAX_THREADS/2; ++index){
			auto my_msg = index;
			if (my_msg %50 == 0) std::cout << "on message " << my_msg << std::endl;
			running_threads.emplace_back(GlobalPool::inst.push([_c = bc->spawn(),
								   &total_time,
								   &total_events,
								   &outlier10_count,
								   &outlier100_count,
								   my_msg](int) mutable -> bool
								  {
									  auto* c = &_c;
					for(unsigned int i = 0; i < 100000; ++i){
						/*time_t old_time = event_counts.at(my_msg);
						auto now = high_resolution_clock::now();
						assert(now - old_time < 30s);
						event_counts.at(my_msg) = now;//*/
						auto average = total_time / (total_events + 1.0);
						//auto outlier10_average = outlier10_count / (total_events + 1.0);
						//auto outlier100_average = outlier100_count / (total_events + 1.0);
						auto start = high_resolution_clock::now();
						static_assert(sizeof(short)*2 == sizeof(int),"");
						int receipt{-1};
						short *receipt_arr = (short*)&receipt;
						c->send(my_msg);
						//std::cout << "sending initial message" << std::endl;
						c->receive(receipt_arr[0]);
						c->receive(receipt_arr[1]);
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
							assert(c);
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
					return true;
								  }));
		}
	}
	catch (const debug_info& exn){
		std::cerr << "message error: " << exn.my_msg << " : " << exn.receipt << std::endl;
	}
	for (auto &t : running_threads){
		t.get();
	}
}
