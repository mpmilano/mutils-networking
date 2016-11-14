#include "dual_connection.hpp"

namespace mutils{

	super_state::super_state(whendebug(std::ofstream &log_file,) new_dualstate_t f, SimpleConcurrentVector<super_state* >& clearing_house, std::atomic_int &used_ids, ::mutils::connection& c)
			:whendebug(log_file(log_file),)
			f(f),
			clearing_house(clearing_house),
			c(c), my_id(used_ids++)
			{
				if (clearing_house.size() <= (unsigned long) my_id) {
					clearing_house.resize(my_id*2 + 1);
				}
				c.send(my_id);
			}
	
			namespace {
			void initialize_super_states(super_state &data_super_state, super_state &control_super_state){
				auto epf = [](auto & ...){assert(false && "do not use this epoll dispatch");};
				auto* data_st =
					&data_super_state.ep.template add<data_state>(
						std::make_unique<data_state>(
							whendebug(data_super_state.log_file,) data_super_state.f, data_super_state.c, control_super_state.c),
						epf);
				data_super_state.child_state = data_st;
				control_super_state.child_state =
					&data_super_state.ep.template add<control_state>(
						std::move(data_st->sibling_tmp_owner),
						epf);
			}
		}
	/*protocol sequence:
	  Person who goes first: put yourself in clearing_house, done.
	  person who goes second: initialize everything for you + your partner.
	*/
	void super_state::deliver_new_event(const void* v){
		if (!child_state){
				assert(my_partner == -1);
				if (my_partner == -1){
					//this is the first time I'm being invoked.
					i_am_data = *((bool*) v);
					my_partner = *((int*) (((bool*)v) + 1));
					assert(my_partner != my_id);
					clearing_house[my_id] = this;
				}
				if (clearing_house[my_partner]) {
					super_state &data_super_state = (i_am_data ? *this : *clearing_house[my_partner]);
					super_state &control_super_state = (i_am_data ? *clearing_house[my_partner] : *this);
					initialize_super_states(data_super_state,control_super_state);
				}
		}
		else child_state -> deliver_new_event(v);
	}
}
