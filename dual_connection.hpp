#pragma once
#include "connection.hpp"
#include "eventfd.hpp"
#include "rpc_api.hpp"
#include "SimpleConcurrentMap.hpp"
#include "ctpl_stl.h"
#include "interruptible_connection.hpp"
#include "epoll.hpp"

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
	using new_dualstate_t = std::function<dualstate_action_t (whendebug(std::ofstream&,) connection& data, connection& control)>;


	struct control_state;
	
	//assuming synhronous initialization in the order
	//control, data, control, data
	template<typename receiver>
	struct dual_connection_receiver {
		std::vector<rpc::ReceiverFun* > clearing_house;
		std::mutex clearing_house_lock;
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
		
		data_state(whendebug(std::ofstream &log_file,) new_dualstate_t f, ::mutils::connection& c)
			:sibling_tmp_owner(new control_state(*this,c)),
			 dw(f(whendebug(log_file,) c, sibling.c ) ){}
		
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

		whendebug(std::ofstream &log_file);
		new_dualstate_t f;
		std::vector<ReceiverFun* >& clearing_house;
		::mutils::connection& c;
		const int my_id;
		
		super_state(whendebug(std::ofstream &log_file,) new_dualstate_t f, std::vector<ReceiverFun* >& clearing_house, std::mutex& clearing_house_lock, std::atomic_int &used_ids, ::mutils::connection& c)
			:whendebug(log_file(log_file),)
			f(f),
			 clearing_house(clearing_house),
			c(c), my_id(used_ids++)
			{
				if (clearing_house.size() <= (unsigned long) my_id) {
					std::cerr << "this is not safe! need concurrent vector!" << std::endl;
					std::unique_lock<std::mutex> l{clearing_house_lock};
					clearing_house.resize(my_id*2 + 1);
				}
				c.send(my_id);
			}
		
		void deliver_new_event(const void* v){
			if (!child_state){
				bool i_am_data = *((bool*) v);
				int my_partner = *((int*) (((bool*)v) + 1));
				auto epf = [](auto & ...){assert(false && "do not use this epoll dispatch");};
				if (clearing_house[my_id]) child_state = clearing_house[my_id];
				else {
					assert(!clearing_house[my_partner]);
					auto *data_st = &ep.template add<data_state>(std::make_unique<data_state>(whendebug(log_file,) f, c), epf);
					auto *control_st = &ep.template add<control_state>(std::move(data_st->sibling_tmp_owner),epf);
					if (i_am_data) child_state = data_st;
					else child_state = control_st;
				}
			}
			else child_state -> deliver_new_event(v);
		}
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
		:r(port,[f,this](whendebug(std::ofstream &log_file,)
					::mutils::connection &c) -> std::unique_ptr<rpc::ReceiverFun>{
			   return std::unique_ptr<rpc::ReceiverFun>{new super_state(whendebug(log_file,) f, clearing_house, clearing_house_lock, used_ids, c)};
				})
			{}
}
