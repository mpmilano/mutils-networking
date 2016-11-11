#include "dual_connection.hpp"

namespace mutils{


		ControlChannel::ControlChannel(dual_connection& parent, const char first_byte)
			:parent(parent),
			 first_byte(first_byte){}
		
		std::size_t ControlChannel::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
			if (!consumed_first_byte){
				consumed_first_byte = true;
				assert(how_many > 0 && sizes[0] > 0);
				((char*)bufs[0])[0] = first_byte;
				std::size_t new_sizes[how_many];
				memcpy(new_sizes,sizes,how_many*sizeof(std::size_t));
				new_sizes[0] -= 1;
				char* new_bufs[how_many];
				//just copy the *pointers*
				memcpy(new_bufs,bufs,how_many);
				//buf starts one byte later
				new_bufs[0] +=1;
				return parent.i->control->raw_receive(how_many,new_sizes,(void**)new_bufs);
			}
			else return parent.i->control->raw_receive(how_many,sizes,bufs);
		}

		std::size_t ControlChannel::raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs){
			return parent.i->control->raw_send(how_many,sizes,bufs);
		}
		
		bool ControlChannel::valid () const {return parent.i->control->valid();}
		
		const char* ControlChannel::what() const noexcept {
			return "Received control channel message: likely indicates protocol exception";
		}
		
		ControlChannel::~ControlChannel(){
			parent.i->control_exn_thrown = false;
			assert(parent.i->check_control_exn.n_idle() == 1);
			parent.i->exn_first_byte = parent.i->check_control_exn.push(parent.i->check_control_fun);
			parent.i->data->clear_interrupt();
		}


	dual_connection::Internals::Internals(std::unique_ptr<interruptible_connection> data, std::unique_ptr<interruptible_connection> control)
			:data(std::move(data)),
			 control(std::move(control)),
			 check_control_fun([this](int) -> char { 
					 char first_byte;
					 constexpr const std::size_t how_many = 1;
					 void* bufs[] = {&first_byte};
					 this->control->raw_receive(how_many,&how_many,bufs);
					 this->control_exn_thrown = true;
					 this->data->interrupt();
					 return first_byte;
				 }),
			 exn_first_byte(check_control_exn.push(check_control_fun))
			 {}//*/

	dual_connection::dual_connection(std::unique_ptr<interruptible_connection> data, std::unique_ptr<interruptible_connection> control)
		:i(new Internals(std::move(data),std::move(control))){}

	dual_connection::dual_connection(dual_connection&& o)
		:i(std::move(o.i)){}
		
		std::size_t dual_connection::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
			using namespace std::chrono;
			if (i->control_exn_thrown){
				assert(i->exn_first_byte.wait_for(0s) == std::future_status::ready);
				char first_byte = i->exn_first_byte.get();
				//the destructor of this exception will reset the control channel exn flag,
				//and will also re-start the thread that checks for whether the channel is throwing an exception.
				throw ControlChannel{*this,first_byte};
			}
			else try {
					return i->data->raw_receive(how_many,sizes,bufs);
				}
				catch (const ReadInterruptedException&){
					//this means we must have gotten a response on the control channel.
					assert(i->control_exn_thrown);
					throw ControlChannel{*this,i->exn_first_byte.get()};
				}
		}
	
	//this *must* be wait-free.  We're calling it in the receive thread!
	void control_state::deliver_new_event(const void* v){
		sibling->dw->deliver_new_control_event(v);
	}
	
	void control_state::async_tick() {
		//This async_tick doesn't do anything;
		//it will definitely never be called
		}
	
	int control_state::underlying_fd(){
		return block_forever.underlying_fd();
	}
}
