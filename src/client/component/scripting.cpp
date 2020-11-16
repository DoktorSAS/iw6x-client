#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "utils/hook.hpp"

namespace scripting
{
	namespace
	{
		class script_value
		{
		public:
			game::VariableValue value{0, game::VAR_UNDEFINED};

			script_value(const game::VariableValue& value_)
			{
				this->assign(value_);
			}

			script_value(const script_value& other) noexcept
			{
				this->operator=(other);
			}

			script_value(script_value&& other) noexcept
			{
				this->operator=(std::move(other));
			}

			script_value& operator=(const script_value& other) noexcept
			{
				if (this != &other)
				{
					this->release();
					this->assign(other.value);
				}

				return *this;
			}

			script_value& operator=(script_value&& other) noexcept
			{
				if (this != &other)
				{
					this->release();
					this->value = other.value;
					other.value.type = game::VAR_UNDEFINED;
				}

				return *this;
			}

			~script_value()
			{
				this->release();
			}

		private:
			void assign(const game::VariableValue& value_)
			{
				this->value = value_;
				game::AddRefToValue(this->value.type, this->value.u);
			}

			void release()
			{
				if (this->value.type != game::VAR_UNDEFINED)
				{
					game::RemoveRefToValue(this->value.type, this->value.u);
					this->value.type = game::VAR_UNDEFINED;
				}
			}
		};

		class entity final
		{
		public:
			entity()
				: entity(0)
			{
			}

			entity(const entity& other) : entity(other.entity_id_)
			{
			}

			entity(entity&& other) noexcept
			{
				this->entity_id_ = other.entity_id_;
				other.entity_id_ = 0;
			}

			entity(unsigned int entity_id)
				: entity_id_(entity_id)
			{
				this->add();
			}

			~entity()
			{
				this->release();
			}

			entity& operator=(const entity& other)
			{
				if (&other != this)
				{
					this->release();
					this->entity_id_ = other.entity_id_;
					this->add();
				}

				return *this;
			}

			entity& operator=(entity&& other) noexcept
			{
				if (&other != this)
				{
					this->release();
					this->entity_id_ = other.entity_id_;
					other.entity_id_ = 0;
				}

				return *this;
			}

			unsigned int get_entity_id() const
			{
				return this->entity_id_;
			}

			game::scr_entref_t get_entity_reference() const
			{
				return game::Scr_GetEntityIdRef(this->get_entity_id());
			}

		private:
			unsigned int entity_id_;

			void add() const
			{
				if (this->entity_id_)
				{
					game::AddRefToValue(game::VAR_POINTER, {static_cast<int>(this->entity_id_)});
				}
			}

			void release() const
			{
				if (this->entity_id_)
				{
					game::RemoveRefToValue(game::VAR_POINTER, {static_cast<int>(this->entity_id_)});
				}
			}
		};

		struct event
		{
			std::string name;
			unsigned int entity_id{};
			std::vector<script_value> arguments;
		};

		class stack_isolation final
		{
		public:
			stack_isolation()
			{
				this->in_param_count_ = game::scr_VmPub->inparamcount;
				this->out_param_count_ = game::scr_VmPub->outparamcount;
				this->top_ = game::scr_VmPub->top;
				this->max_stack_ = game::scr_VmPub->maxstack;

				game::scr_VmPub->top = this->stack_;
				game::scr_VmPub->maxstack = &this->stack_[ARRAYSIZE(this->stack_) - 1];
				game::scr_VmPub->inparamcount = 0;
				game::scr_VmPub->outparamcount = 0;
			}

			~stack_isolation()
			{
				game::Scr_ClearOutParams();
				game::scr_VmPub->inparamcount = this->in_param_count_;
				game::scr_VmPub->outparamcount = this->out_param_count_;
				game::scr_VmPub->top = this->top_;
				game::scr_VmPub->maxstack = this->max_stack_;
			}

		private:
			game::VariableValue stack_[512]{};

			game::VariableValue* max_stack_;
			game::VariableValue* top_;
			unsigned int in_param_count_;
			unsigned int out_param_count_;
		};

		using script_function = void(*)(game::scr_entref_t);

#pragma warning(push)
#pragma warning(disable: 4611)
		bool call(const script_function function, const game::scr_entref_t& entref)
		{
			*game::g_script_error_level += 1;
			if (setjmp(game::g_script_error[*game::g_script_error_level]))
			{
				*game::g_script_error_level -= 1;
				return false;
			}

			function(entref);

			*game::g_script_error_level -= 1;
			return true;
		}
#pragma warning(pop)

		void safe_call(const script_function function, const game::scr_entref_t& entref)
		{
			if (!call(function, entref))
			{
				throw std::runtime_error("Error executing function");
			}
		}

		script_function get_function_by_index(const unsigned index)
		{
			if (index < 0x25D)
			{
				return reinterpret_cast<script_function*>(SELECT_VALUE(0x144E1E6F0, 0x1446B77A0))[index];
			}

			return reinterpret_cast<script_function*>(SELECT_VALUE(0x144E1F9E0, 0x1446B8A90))[index - 0x8000];
		}

		game::VariableValue* allocate_argument()
		{
			game::VariableValue* value_ptr = ++game::scr_VmPub->top;
			++game::scr_VmPub->inparamcount;
			return value_ptr;
		}

		void push_string(const std::string& string)
		{
			auto* value_ptr = allocate_argument();
			value_ptr->type = game::VAR_STRING;
			value_ptr->u.stringValue = game::SL_GetString(string.data(), 0);
		}

		void test_call(const event& e)
		{
			const entity player(e.entity_id);
			const auto function = get_function_by_index(0x8264); // iclientprintlnbold

			stack_isolation _;

			push_string("^1Hello ^2player^5!");

			game::scr_VmPub->outparamcount = game::scr_VmPub->inparamcount;
			game::scr_VmPub->inparamcount = 0;

			safe_call(function, player.get_entity_reference());
		}

		utils::hook::detour vm_notify_hook;

		void vm_notify_stub(const unsigned int notify_list_owner_id, const game::scr_string_t string_value,
		                    game::VariableValue* top)
		{
			const auto* string = game::SL_ConvertToString(string_value);
			if (string)
			{
				event e;
				e.name = string;
				e.entity_id = notify_list_owner_id;

				for (auto* value = top; value->type != game::VAR_PRECODEPOS; --value)
				{
					e.arguments.emplace_back(*value);
				}

#ifdef DEV_BUILD
				if (e.name == "spawned_player")
				{
					test_call(e);
				}
#endif
			}

			vm_notify_hook.invoke<void>(notify_list_owner_id, string_value, top);
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			vm_notify_hook.create(SELECT_VALUE(0x1403E29C0, 0x14043D9B0), vm_notify_stub);
		}
	};
}

REGISTER_COMPONENT(scripting::component)