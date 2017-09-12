#include "dual_connection.hpp"
#include "batched_connection.hpp"
#include <unistd.h>

using namespace mutils;
using namespace std::chrono;

constexpr int port = 9876;

using duration_t = DECT(high_resolution_clock::now() - high_resolution_clock::now());
struct data_pack{duration_t elapsed_data{0s}; std::size_t count_data{0}; duration_t elapsed_control{0s}; std::size_t count_control{0};};

void failonfalse(bool b){
	if (!b){
		std::cout << "fail on false!" << std::endl;
		std::exit(1);
	}
}

void data_sender_body(data_pack& p, duration_t &cumulative_latency, std::size_t &total_trips, mutils::connection& c){
	using namespace std::chrono;
	auto i = long_rand();
	auto start = high_resolution_clock::now();
	c.send(i);
	try {
		failonfalse(i+1 == *std::get<0>(c.receive<decltype(i)>()));
		cumulative_latency += high_resolution_clock::now() - start;
		++total_trips;
	} catch(ControlChannel& cc){
		failonfalse(i+1 == *std::get<0>(cc.receive<decltype(i)>()));
		data_sender_body(p, p.elapsed_control, p.count_control, cc);
	}
	if (i % 100 == 1) {
		std::stringstream ss;
		std::cout << ss.str();
	}
}

void data_receiver_body(mutils::connection& c, char const * const buf){
	c.send(1 + *from_bytes_noalloc<DECT(long_rand())>(nullptr,buf));
}

void choose_socket(mutils::connection& d, mutils::connection& c, char const * const buf){
	if (int_rand() % 10 == 0){
		data_receiver_body(c,buf);
	}
	else data_receiver_body(d,buf);
}

dualstate_action_t receive_fun(whendebug(std::ostream& ,) connection& data, connection& control){
	struct Receiver : public dual_state_receiver {
		connection &data;
		connection &control;
		
		Receiver(connection &data, connection &control)
			:data(data),control(control){}
		
		eventfd notice;
		//this *must* be wait-free.  We're calling it in the receive thread!
		void deliver_new_data_event(const void* buf) {
			failonfalse(&data != &control);
			choose_socket(data,control,(const char*)buf);
		}
		
		//this *must* be wait-free.  We're calling it in the receive thread!
		void deliver_new_control_event(const void* buf) {
			data_receiver_body(control,(const char*)buf);
		}
		
		//this *must* be wait-free.  We're calling it in the receive thread!
		void async_tick() {}
		//must be able to select() on this int as an FD
		//where a "read" ready indicates it's time to
		//call async_tick
		int underlying_fd() {
			return notice.underlying_fd();
		}
	};
	
	return dualstate_action_t{new Receiver(data,control)};
}

void receiver_loop(){
	dual_connection_receiver<batched_connection::receiver> dcr(
		port,new_dualstate_t{receive_fun});
	dcr.acceptor_fun();
}

constexpr unsigned short num_connections = 500;


struct data_vector : public std::vector<data_pack>{
	template<typename... T>
	data_vector(T&&... t):
		std::vector<data_pack>(std::forward<T>(t)...){}

	void print_averages(){
		std::vector<data_pack> &_this = *this;
		const auto num_entries = _this.size();
		std::size_t data_averages{0};
		std::size_t control_averages{0};
		for (auto& p : _this){
			if (p.count_data > 0){
				data_averages += duration_cast<microseconds>(p.elapsed_data).count() / p.count_data;
			}
			if (p.count_control > 0){
				control_averages += duration_cast<microseconds>(p.elapsed_control).count() / p.count_control;
			}
		}
		std::cout << "overall average latency: data: " << data_averages / num_entries << " control: " << control_averages / num_entries << std::endl;
	}
	
	~data_vector(){
		print_averages();
	}
};

int main(){
	using namespace std;
	using namespace chrono;
	std::thread receiver_thread{receiver_loop};
	{
		std::this_thread::sleep_for(1s);
		dual_connection_manager<batched_connection::connections> dcm(
			decode_ip("127.0.0.1"),port,num_connections);
		data_vector data{num_connections};
		for (std::size_t i = 0; i < num_connections;){
			using namespace std;
			using namespace chrono;
			std::this_thread::sleep_for(100ms);
			if (int_rand() % 10 == 1) {
				
				std::thread{[&data_p = data[i], c = dcm.spawn()]() mutable {
						std::cout << "launched" << std::endl;
						
						while (true) data_sender_body(data_p,data_p.elapsed_data, data_p.count_data, c);
					}}.detach();
				++i;
			}
		}

		this_thread::sleep_for(100s);
		std::cout << "end of control reached" << std::endl;
		data.print_averages();
		throw 3;
		
	}
}
