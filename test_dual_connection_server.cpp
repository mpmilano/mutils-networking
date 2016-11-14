#include "dual_connection.hpp"
#include "batched_connection.hpp"

using namespace mutils;

int main(){
	dual_connection_receiver<batched_connection::receiver> dcr(
		9876,[](whendebug(std::ofstream& ,) connection& data, connection& control){

			struct Receiver : public dual_state_receiver {
				connection &data;
				connection &control;

				Receiver(connection &data, connection &control)
					:data(data),control(control){}
				
				eventfd notice;
				//this *must* be wait-free.  We're calling it in the receive thread!
				void deliver_new_data_event(const void*) {
					assert(&data != &control);
					int e = 42;
					data.send(e);
					control.send(e);
				}
				
				//this *must* be wait-free.  We're calling it in the receive thread!
				void deliver_new_control_event(const void*) {
					assert(false);
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
		});
	dcr.acceptor_fun();
}
