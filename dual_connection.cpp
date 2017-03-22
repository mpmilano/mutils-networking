#include "dual_connection.hpp"

namespace mutils{


		ControlChannel::ControlChannel(dual_connection& parent, connection& data_channel)
			:parent(parent)
			,data_channel(data_channel)
		{}
		
		std::size_t ControlChannel::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
			return parent.i->control->raw_receive(how_many,sizes,bufs);
		}

		std::size_t ControlChannel::raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs){
			return parent.i->control->raw_send(how_many,sizes,bufs);
		}
		
		bool ControlChannel::valid () const {return parent.i->control->valid();}

#ifndef NDEBUG
	std::ostream& ControlChannel::get_log_file(){
		return parent.i->control->get_log_file();
	}
#endif
		
		const char* ControlChannel::what() const noexcept {
			return "Received control channel message: likely indicates protocol exception";
		}
		
		ControlChannel::~ControlChannel(){
			parent.i->control_exn_thrown = false;
			assert(parent.i->check_control_exn.n_idle() == 1);
			parent.i->check_control_exn.push(parent.i->check_control_fun);
			parent.i->data->clear_interrupt();
		}

	dual_connection::~dual_connection(){
		i->control->interrupt();
	}


	dual_connection::Internals::Internals(std::unique_ptr<interruptible_connection> data, std::unique_ptr<interruptible_connection> control)
			:data(std::move(data)),
			 control(std::move(control)),
			 check_control_fun([this](int) -> void {
					 this->control->raw_receive(0,nullptr,nullptr);
					 this->control_exn_thrown = true;
					 this->data->interrupt();
				 })
	{check_control_exn.push(check_control_fun);}//*/

	dual_connection::dual_connection(std::unique_ptr<interruptible_connection> data, std::unique_ptr<interruptible_connection> control)
		:i(new Internals(std::move(data),std::move(control))){}

	dual_connection::dual_connection(dual_connection&& o)
		:i(std::move(o.i)){}
		
		std::size_t dual_connection::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
			using namespace std::chrono;
			if (i->control_exn_thrown){
				//the destructor of this exception will reset the control channel exn flag,
				//and will also re-start the thread that checks for whether the channel is throwing an exception.
				throw ControlChannel{*this,*i->data};
			}
			else try {
					return i->data->raw_receive(how_many,sizes,bufs);
				}
				catch (const ReadInterruptedException&){
					//this means we must have gotten a response on the control channel.
					assert(i->control_exn_thrown);
					throw ControlChannel{*this,*i->data};
				}
		}
	
	//this *must* be wait-free.  We're calling it in the receive thread!
	void control_state::deliver_new_event(const void* v){
		sibling.dw->deliver_new_control_event(v);
	}
	
	void control_state::async_tick() {
		//This async_tick doesn't do anything;
		//it will definitely never be called
		}
	
	int control_state::underlying_fd(){
		return block_forever.underlying_fd();
	}
}
