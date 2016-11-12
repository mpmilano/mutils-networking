#pragma once
#include "connection.hpp"
#include "eventfd.hpp"
#include "rpc_api.hpp"
#include "SimpleConcurrentMap.hpp"
#include "ctpl_stl.h"
#include "interruptible_connection.hpp"

namespace mutils{

	struct dual_connection;
	
	//throw this when we get a message on the control channel
	struct ControlChannel : public std::exception, public connection {
		//this socket is ready for reading and contains the message
		dual_connection& parent;

		ControlChannel(dual_connection& parent);
		
		std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs);

		std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs);
		
		bool valid () const ;

		whendebug(std::ofstream& get_log_file();)
		
		const char* what() const noexcept;
		
		~ControlChannel();
	};

	struct dual_connection : public connection{
		struct Internals{
			std::unique_ptr<interruptible_connection> data;
			std::unique_ptr<interruptible_connection> control;
			std::atomic_bool control_exn_thrown{false};
			ctpl::thread_pool check_control_exn{1};
			std::function<void (int) > check_control_fun;
			Internals(std::unique_ptr<interruptible_connection> data, std::unique_ptr<interruptible_connection> control);
		};
		std::unique_ptr<Internals> i;
			
		dual_connection(std::unique_ptr<interruptible_connection> data, std::unique_ptr<interruptible_connection> control);
		dual_connection(const dual_connection&) = delete;
		dual_connection(dual_connection&& o);

#ifndef NDEBUG
		std::ofstream& get_log_file(){
			return i->data->get_log_file();
		}
#endif
		
		std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs);
		bool valid() const {
			return i->data->valid() && i->control->valid();
		}
		std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const v) {
			return i->data->raw_send(how_many,sizes,v);
		}
		~dual_connection();
	};

	template<typename ConnectionManager>
	struct dual_connection_manager {
		ConnectionManager cm;
		using connection = std::decay_t<decltype(cm.spawn())>;
		static_assert(std::is_base_of<interruptible_connection,connection>::value,
					  "Error: ConnectionManager does not have a spawn() method which supplies connections");
		
		template<typename... Args>
		dual_connection_manager(Args && ... a)
			:cm(std::forward<Args>(a)...){}

		dual_connection spawn(){
			//use of this spawning technique is safe only because
			//the control channel (constructed first) never gets ticked
			auto l = cm.spawn(2);
			using subconn = std::decay_t<decltype(l.front())>;
			bool front_is_data{false};
			bool back_is_data{false};
			l.front().receive(front_is_data);
			l.back().receive(back_is_data);
			assert(front_is_data ? !back_is_data : back_is_data);
			if (front_is_data){
#ifndef NDEBUG
				auto &flog_file = l.front().log_file;
				flog_file << "this is the data connection" << std::endl;
				flog_file.flush();
				auto &blog_file = l.back().log_file;
				blog_file << "this is the control connection" << std::endl;
				blog_file.flush();
#endif
				return dual_connection{
					std::unique_ptr<interruptible_connection>(new subconn(std::move(l.front()))),
						std::unique_ptr<interruptible_connection>(new subconn(std::move(l.back())))
						};
			}
			else {
#ifndef NDEBUG
				auto &flog_file = l.back().log_file;
				flog_file << "this is the data connection" << std::endl;
				flog_file.flush();
				auto &blog_file = l.front().log_file;
				blog_file << "this is the control connection" << std::endl;
				blog_file.flush();
#endif
				return dual_connection{
					std::unique_ptr<interruptible_connection>(new subconn(std::move(l.back()))),
						std::unique_ptr<interruptible_connection>(new subconn(std::move(l.front())))
						};
			}
		}
	};

	struct dual_state_receiver {
		
		//this *must* be wait-free.  We're calling it in the receive thread!
		virtual void deliver_new_data_event(const void*) = 0;

		//this *must* be wait-free.  We're calling it in the receive thread!
		virtual void deliver_new_control_event(const void*) = 0;
		
		//this *must* be wait-free.  We're calling it in the receive thread!
		virtual void async_tick() = 0;
		//must be able to select() on this int as an FD
		//where a "read" ready indicates it's time to
		//call async_tick
		virtual int underlying_fd() = 0;
		virtual ~dual_state_receiver(){}
	};

	using dualstate_action_t = std::unique_ptr<dual_state_receiver>;
	using new_dualstate_t = std::function<dualstate_action_t (whendebug(std::ofstream&,) connection& data, connection& control)>;


	struct control_state;
	
	//assuming synhronous initialization in the order
	//control, data, control, data
	template<typename receiver>
	struct dual_connection_receiver {
		control_state* last_control_state{nullptr};
		receiver r;
		bool &alive{r.alive};

		void acceptor_fun(){
			r.acceptor_fun();
		}
		
		dual_connection_receiver(int port, new_dualstate_t f);
	};
	using control_state_p = control_state*;
	struct data_state;

	struct control_state : public rpc::ReceiverFun {
		control_state_p& last_control_state;
		data_state* sibling{nullptr};
		::mutils::connection& c;
		eventfd block_forever;
		
		control_state(control_state_p& parent, ::mutils::connection& c)
		:last_control_state(parent),c(c)
			{
				bool is_data{false};
				std::cout << "control state constructed" << std::endl;
				last_control_state = this;
				c.send(is_data);
			}

		//this *must* be wait-free.  We're calling it in the receive thread!
		void deliver_new_event(const void* v);
		
		void async_tick();

		int underlying_fd();

	};

	struct data_state : public rpc::ReceiverFun{

		control_state_p &last_control_state;
		control_state& sibling{*last_control_state};
		std::unique_ptr<dual_state_receiver> dw;
		
		data_state(whendebug(std::ofstream &log_file,) new_dualstate_t f, control_state_p& parent, ::mutils::connection& c)
			:last_control_state(parent),dw(f(whendebug(log_file,) c, sibling.c ) ){
			bool is_data{true};
			last_control_state = nullptr;
			std::cout << "setting sibling" << std::endl;
			sibling.sibling = this;
			c.send(is_data);
		}
		
		//this *must* be wait-free.  We're calling it in the receive thread!
		void deliver_new_event(const void* v){
			dw->deliver_new_data_event(v);
			
		}
		//this *must* be wait-free.  We're calling it in the receive thread!
		void async_tick(){
			dw->async_tick();
		}
		//must be able to select() on this int as an FD
		//where a "read" ready indicates it's time to
		//call async_tick
		int underlying_fd(){
			return dw->underlying_fd();
		}
	};

	template<typename r>
	dual_connection_receiver<r>::dual_connection_receiver(int port, new_dualstate_t f)
		:r(port,[f,this](whendebug(std::ofstream &log_file,)
					::mutils::connection &c) -> std::unique_ptr<rpc::ReceiverFun>{
					if (!last_control_state){
						return std::unique_ptr<rpc::ReceiverFun>
						{new control_state{last_control_state,c}};
					}
					else return std::unique_ptr<rpc::ReceiverFun>
						 {new data_state{whendebug(log_file,)f,last_control_state,c}};
				})
			{}

}
