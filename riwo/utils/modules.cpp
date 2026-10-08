// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "modules.h"
#include "logger.h"

#include <unordered_map>
#include <unordered_set>

namespace riwo::utils
{

enum variant_type {
	func0_e, func1_e, func2_e, func3_e, func_state
};
using state_t = detail::modules::state;
using func_obj_t = detail::modules::func_obj_t;

using dependency_t = modules::dependency;
using unexpected_t = modules::unexpected;

class RIWO_DECL_HIDDEN initializer
{
	RIWO_DISABLE_COPY_MOVE(initializer)
	initializer() = default;

	struct node_t
	{
		func_obj_t init;
		std::atomic_size_t counter {0};
		std::atomic_bool success {true};

		using ptr_t = std::shared_ptr<node_t>;
		std::unordered_map<std::string,ptr_t> children {};
	};
	using node_ptr = node_t::ptr_t;
	using dsd_t = std::unordered_map<std::string,node_ptr>;

	struct unexpected_offsets
	{
		std::atomic_size_t failures {0};
		std::atomic_size_t unregistered {0};
		std::atomic_size_t children {0};
	};

public:
	[[nodiscard]] static initializer &instance() noexcept
	{
		static initializer obj;
		return obj;
	}

public:
	void join(std::string name, const dependency_t &depy, func_obj_t func)
	{
		if( name.empty() )
		{
			runtime_error::loc_throw (
				"riwo::modules::reg_init: Empty module name."
			);
		}
		if( m_names.contains(name) )
		{
			runtime_error::loc_throw(std::format (
				"riwo::modules::reg_init: Module name '{}' already registered.",
				name
			));
		}
		auto res = [&func]() -> bool
		{
			if( func.index() == func0_e )
				return std::get<detail::modules::func0_t>(func) != nullptr;
			else if( func.index() == func1_e )
				return std::get<detail::modules::func1_t>(func) != nullptr;
			else if( func.index() == func2_e )
				return std::get<detail::modules::func2_t>(func) != nullptr;
			// else if( func.index() == func3_e )
			return std::get<detail::modules::func3_t>(func) != nullptr;
		}();
		if( not res )
		{
			runtime_error::loc_throw (
				"riwo::modules::reg_init: Invalid function object."
			);
		}
		dsd_emplace(name, depy, std::move(func));
		m_names.emplace(std::move(name));
	}

	void operator()(string_vector args, std::function<void(unexpected_t)> callback)
	{
		if( m_names.empty() )
			return ;

		else if( m_counter == 0 )
		{
			runtime_error::loc_throw (
				"riwo::modules::reg_init: Initialization has been completed. Do not call again."
			);
		}
		auto cycle = detect_cycle();
		if( not cycle.empty() )
		{
			std::string text;
			for(size_t i=0; i<cycle.size()-1; i++)
				text += cycle[i] + " -> ";
			text += cycle.back();

			runtime_error::loc_throw(std::format (
				"riwo::modules::reg_init: Circular dependency detected: {}",
				text
			));
		}
		init(std::move(args), std::move(callback));
	}

public:
	[[nodiscard]] std::string sprint_dsd() const noexcept
	{
		std::string buffer;
		if( m_dsd.empty() )
		{
			buffer = "empty dsd";
			return buffer;
		}
		std::unordered_set<node_ptr> fully_expanded;
		std::unordered_set<node_ptr> current_path;
		auto it = m_dsd.begin();

		for(; std::next(it)!=m_dsd.end(); ++it)
		{
			sprint_node(it->second, it->first,
				fully_expanded, current_path, buffer, "", false
			);
		}
		sprint_node(it->second, it->first,
			fully_expanded, current_path, buffer, "", true
		);
		buffer.pop_back();
		return buffer;
	}

private:
	void dsd_emplace(std::string name, const dependency_t &depy, func_obj_t func)
	{
		auto [sub_dsd, sub_it] = find_node(name);
		if( sub_it == sub_dsd->end() )
		{
			auto node = make_node(std::move(func));
			children_reorganize(node, depy.children);

			if( depy.parents.empty() )
			{
				node->counter = 1;
				m_dsd.emplace(std::move(name), std::move(node));
				return ;
			}
			return parent_reorganize (
				name, node, depy.parents
			);
		}
		auto sub_node = sub_it->second;
		sub_node->init = std::move(func);

		children_reorganize(sub_node, depy.children);
		if( depy.parents.empty() )
			return ;

		else if( sub_dsd == &m_dsd )
		{
			m_dsd.erase(sub_it);
			sub_node->counter = 0;
		}
		parent_reorganize (
			name, sub_node, depy.parents
		);
	}

	void children_reorganize(const node_ptr &node, const string_set &children)
	{
		for(auto &child : children)
		{
			auto [child_dsd, child_it] = find_node(child);
			node_ptr child_node {};

			if( child_it == child_dsd->end() )
				child_node = make_node(state_t::not_register);
			else
			{
				child_node = child_it->second;
				if( child_dsd == &m_dsd )
				{
					m_dsd.erase(child_it);
					child_node->counter = 0;
				}
			}
			++child_node->counter;
			node->children.emplace(child, std::move(child_node));
		}
	}

	void parent_reorganize(const std::string &name, const node_ptr &node, const string_set &parents)
	{
		for(auto &parent : parents)
		{
			auto [parent_dsd, parent_it] = find_node(parent);
			node_ptr parent_node {};

			if( parent_it == parent_dsd->end() )
			{
				parent_node = make_node(state_t::not_register);
				parent_node->counter = 1;
				m_dsd.emplace(parent, parent_node);
			}
			else
				parent_node = parent_it->second;

			parent_node->children.emplace(name, node);
			++node->counter;
		}
	}

private:
	void init(string_vector init_args, std::function<void(unexpected_t)> completion)
	{
		std::thread([this, args = std::move(init_args), callback = std::move(completion)]
		{
			unexpected_t unexpected;
			unexpected_offsets offsets;
			const auto maximum_results = m_counter.load(std::memory_order_acquire);

			unexpected.failures.resize(maximum_results);
			unexpected.unregistered.resize(maximum_results);
			unexpected.children.resize(maximum_results);

			do_init(m_dsd, true, args, unexpected, offsets);

			for(auto remaining = m_counter.load(std::memory_order_acquire);
				remaining != 0; remaining = m_counter.load(std::memory_order_acquire))
			{
				m_counter.wait(remaining, std::memory_order_acquire);
			}
			unexpected.failures.resize (
				offsets.failures.load(std::memory_order_acquire)
			);
			unexpected.unregistered.resize (
				offsets.unregistered.load(std::memory_order_acquire)
			);
			unexpected.children.resize (
				offsets.children.load(std::memory_order_acquire)
			);
			if( callback )
				callback(std::move(unexpected));
		})
		.detach();
	}

	void do_init(const dsd_t &nodes, bool initial_success, const string_vector &args,
		unexpected_t &unexpected, unexpected_offsets &offsets)
	{
		for(auto &[name, node] : nodes)
		{
			if( node->success )
				node->success = initial_success;

			if( --node->counter > 0 )
				continue;

			std::thread([this,
				name, node, success = node->success.load(),
				&args, &unexpected, &offsets
			]() mutable
			{
				if( node->init.index() != func_state )
				{
					if( success )
					{
						riwo_utils_clog_info("Riwo.Utils",
							"modules: <{}> initializing ...", name
						);
						if( node->init.index() == func0_e )
							success = std::get<detail::modules::func0_t>(std::move(node->init))();

						else if( node->init.index() == func1_e )
							success = std::get<detail::modules::func1_t>(std::move(node->init))(args);

						else if( node->init.index() == func2_e )
							std::get<detail::modules::func2_t>(std::move(node->init))();

						else if( node->init.index() == func3_e )
							std::get<detail::modules::func3_t>(std::move(node->init))(args);

						if( success )
						{
							riwo_utils_clog_info("Riwo.Utils",
								"modules: <{}> ok.", name
							);
						}
						else
						{
							riwo_utils_clog_error("Riwo.Utils",
								"modules: <{}> failed.", name
							);
							const auto index = offsets.failures.fetch_add (
								1, std::memory_order_relaxed
							);
							unexpected.failures[index] = name;
						}
					}
					else
					{
						riwo_utils_clog_warning("Riwo.Utils",
							"modules: <{}> cannot be initialized "
							"because the parent module failed to initialize.",
							name
						);
						const auto index = offsets.children.fetch_add (
							1, std::memory_order_relaxed
						);
						unexpected.children[index] = name;
					}
					node->init = detail::modules::state::finished;
				}
				else if( std::get<state_t>(std::move(node->init)) == state_t::not_register )
				{
					riwo_utils_clog_error("Riwo.Utils",
						"modules: <{}> is not registered.", name
					);
					const auto index = offsets.unregistered.fetch_add (
						1, std::memory_order_relaxed
					);
					unexpected.unregistered[index] = name;
					success = false;
				}
				if( try_notify() )
					return ;

				do_init(node->children, success, args,
					unexpected, offsets
				);
			})
			.detach();
		}
	}

	[[nodiscard]] bool try_notify() noexcept
	{
		if( m_counter.fetch_sub(1, std::memory_order_acq_rel) == 1 )
		{
			m_counter.notify_all();
			return true;
		}
		return false;
	}

private:
	[[nodiscard]] std::pair<dsd_t*,dsd_t::iterator> find_node(std::string_view name) noexcept {
		return do_find_node(&m_dsd, name);
	}

	[[nodiscard]] static std::pair<dsd_t*,dsd_t::iterator> do_find_node
	(dsd_t *dsd, std::string_view name) noexcept
	{
		for(auto it=dsd->begin(); it!=dsd->end(); ++it)
		{
			if( it->first == name )
				return { dsd, it };

			if( auto [_dsd, _it] = do_find_node(&it->second->children, name);
				_it != _dsd->end() )
				return { _dsd, _it };
		}
		return { dsd, dsd->end() };
	}

	[[nodiscard]] node_ptr make_node(func_obj_t init) noexcept
	{
		auto n = std::make_shared<node_t>();
		n->init = std::move(init);
		++m_counter;
		return n;
	}

private:
	[[nodiscard]] std::vector<std::string> detect_cycle()
	{
		std::unordered_set<std::string> visited; // Fast record stack
		std::vector<std::string> path; // Record stack (in reverse order)

		do_detect_cycle(m_dsd, visited, path);
		std::ranges::reverse(path);
		return path;
	}

	static bool do_detect_cycle
	(const dsd_t &dsd, std::unordered_set<std::string> &visited, std::vector<std::string> &path)
	{
		for(auto &[child_name, child_node] : dsd)
		{
			// Push stack.
			// If the nodes are duplicated, it indicates the presence of a loop.
			if( auto [it, inserted] = visited.emplace(child_name); not inserted )
			{
				path.emplace_back(child_name);
				break;
			}
			// Deep-first search.
			if( do_detect_cycle(child_node->children, visited, path) )
				return true;

			// Pop stack.
			visited.erase(child_name);
			if( not path.empty() )
			{
				// Push stack.
				path.emplace_back(child_name);

				// Stack is full.
				if( path.front() == child_name )
					return true;
				break;
			}
		}
		return false;
	}

public:
	static void sprint_node(const node_ptr &node,
							const std::string &name,
							std::unordered_set<node_ptr> &fully_expanded,
							std::unordered_set<node_ptr> &current_path,
							std::string &buffer,
							const std::string &prefix,
							bool is_last)
	{
		if( current_path.contains(node) )
		{
			buffer += std::format("{}{}{} (Circular)\n",
				prefix, is_last ? "└─" : "├─", name
			);
			return ;
		}
		bool is_already_expanded = fully_expanded.contains(node);
		std::string reg_state;

		if( node->init.index() == func_state )
		{
			if( std::get<state_t>(node->init) == state_t::not_register )
				reg_state = " [NoReg]";
		}
		buffer += std::format("{}{}{}{}{}\n",
			prefix, is_last ? "└─" : "├─", name, reg_state,
			is_already_expanded ? " (Expanded)" : ""
		);
		if( is_already_expanded )
			return ;

		// Detect whether there is a circular reference.
		current_path.emplace(node);

		auto child_prefix = prefix + (is_last ? "  " : "│ ");
		if( auto &children = node->children; not children.empty() )
		{
			auto it = children.begin();
			for(; std::next(it)!=children.end(); ++it)
			{
				sprint_node(it->second, it->first,
					fully_expanded, current_path, buffer, child_prefix, false
				);
			}
			sprint_node(it->second, it->first,
				fully_expanded, current_path, buffer, child_prefix, true
			);
		}
		// Pop stack.
		current_path.erase(node);
		fully_expanded.emplace(node);
	}

private:
	std::unordered_set<std::string> m_names {};  // Used solely for repetitive testing.
	std::atomic_size_t m_counter {0};
	dsd_t m_dsd {};
};

namespace detail
{

void modules::reg_init(std::string name, const dependency_t &depy, func_obj_t func)
{
	initializer::instance().join(std::move(name), depy, std::move(func));
}

void modules::do_init(const string_vector &args, std::function<void(unexpected_t)> callback)
{
	initializer::instance()(args, std::move(callback));
}

} //namespace detail

std::string modules::sprint() noexcept
{
	return initializer::instance().sprint_dsd();
}

} //namespace riwo::utils
