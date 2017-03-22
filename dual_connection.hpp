#pragma once
#include "connection.hpp"
#include "eventfd.hpp"
#include "rpc_api.hpp"
#include "SimpleConcurrentMap.hpp"
#include "ctpl_stl.h"
#include "interruptible_connection.hpp"
#include "epoll.hpp"
#include "SimpleConcurrentVector.hpp"

namespace mutils{

	struct dual_connection;
	
	//throw this when we get a message on the control channel
	struct ControlChannel : public std::exception, public connection {
		//this socket is ready for reading and contains the message
		dual_connection& parent;
		connection& data_channel;

		ControlChannel(dual_connection& parent, connection& data_channel);
		
		std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs);

		std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs);
		
		bool valid () const ;

		whendebug(std::ostream& get_log_file();)
		
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
		std::ostream& get_log_file(){
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
			auto &data = l.front();
			auto &control = l.back();

			int data_id{-2};
			int control_id{-2};
			data.receive(data_id);
			control.receive(control_id);
			data.send(true,control_id);
			control.send(false,data_id);
			return dual_connection{
				std::unique_ptr<interruptible_connection>(new subconn(std::move(l.front()))),
					std::unique_ptr<interruptible_connection>(new subconn(std::move(l.back())))
					};
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
	using new_dualstate_t = std::function<dualstate_action_t (whendebug(std::ostream&,) connection& data, connection& control)>;


	struct control_state;
	struct super_state;
	
	//assuming synhronous initialization in the order
	//control, data, control, data
	template<typename receiver>
	struct dual_connection_receiver {
		SimpleConcurrentVector<super_state* > clearing_house;
		std::atomic_int used_ids{0};
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
		data_state& sibling;
		::mutils::connection& c;
		eventfd block_forever;
		
		control_state(data_state& sibling, ::mutils::connection& c):sibling(sibling),c(c)
			{}

		//this *must* be wait-free.  We're calling it in the receive thread!
		void deliver_new_event(const void* v);
		
		void async_tick();

		int underlying_fd();

	};

	struct data_state : public rpc::ReceiverFun{

		std::unique_ptr<control_state> sibling_tmp_owner;
		control_state& sibling{*sibling_tmp_owner};
		std::unique_ptr<dual_state_receiver> dw;
		
		data_state(whendebug(std::ostream &log_file,) new_dualstate_t f,
				   ::mutils::connection& data_conn, ::mutils::connection& control_conn)
			:sibling_tmp_owner(new control_state(*this,control_conn)),
			 dw(f(whendebug(log_file,) data_conn, control_conn ) ){}
		
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

	struct super_state : public rpc::ReceiverFun{
		EPoll ep;
		eventfd &never{ep.template add<eventfd>(std::make_unique<eventfd>(), [](EPoll&, eventfd&){})};
		rpc::ReceiverFun* child_state{nullptr};
		//this *must* be wait-free.  We're calling it in the receive thread!

		whendebug(std::ostream &log_file);
		new_dualstate_t f;
		SimpleConcurrentVector<super_state* >& clearing_house;
		::mutils::connection& c;
		const int my_id;
		int my_partner{-1};
		bool i_am_data{false};
		
		super_state(whendebug(std::ostream &log_file,) new_dualstate_t f, SimpleConcurrentVector<super_state* >& clearing_house, std::atomic_int &used_ids, ::mutils::connection& c);
		

		/*protocol sequence:
		  Person who goes first: put yourself in clearing_house, done.
		  person who goes second: initialize everything for you + your partner.
		 */
		void deliver_new_event(const void* v);
		//this *must* be wait-free.  We're calling it in the receive thread!
		void async_tick(){
			if (child_state) child_state->async_tick();
		}
		//must be able to select() on this int as an FD
		//where a "read" ready indicates it's time to
		//call async_tick
		int underlying_fd(){
			return ep.underlying_fd();
		}
	};

	template<typename r>
	dual_connection_receiver<r>::dual_connection_receiver(int port, new_dualstate_t f)
		:r(port,[f,this](whendebug(std::ostream &log_file,)
					::mutils::connection &c) -> std::unique_ptr<rpc::ReceiverFun>{
			   return std::unique_ptr<rpc::ReceiverFun>{new super_state(whendebug(log_file,) f, clearing_house, used_ids, c)};
				})
			{}
}
