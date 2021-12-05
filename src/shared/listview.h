#pragma once

// A helper class to hold a reference to a "global" list
// with "local" views on top of that list. Used e.g. to store
// information about files in a directory while having those
// files spread freely on the disc/array.

#include <algorithm>
#include <list>
#include <utility>
#include <vector>

template<typename T>
class ListView
{
public:
	using type = T;

	explicit ListView(std::list<T>& list)
		: m_list(list)
	{
	}

	// Create a new view over the same list
	ListView NewView() const
	{
		return ListView(m_list);
	}

	template<typename... Args>
	auto& emplace(Args&&... args)
	{
		auto& ref = m_list.emplace_back(std::forward<Args>(args)...);
		m_view.emplace_back(ref);
		return ref;
	}

	template<typename Compare>
	void SortView(Compare&& comp)
	{
		std::sort(m_view.begin(), m_view.end(), std::forward<Compare>(comp));
	}

	// Access to the view.
	std::vector<std::reference_wrapper<type>>& GetView() { return m_view; }
	const std::vector<std::reference_wrapper<type>>& GetView() const { return m_view; }

	// Access to the entire list. Use sparingly.
	std::list<T>& GetUnderlyingList() { return m_list; }
	const std::list<T>& GetUnderlyingList() const { return m_list; }

private:
	std::vector<std::reference_wrapper<type>> m_view;
	std::list<T>& m_list;
};
