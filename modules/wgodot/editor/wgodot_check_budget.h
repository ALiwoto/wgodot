// wgodot-changes::file
#pragma once

#include <atomic>

// Shared by the script and resource jobs of one check. Only the counter is
// shared: each worker retains its own diagnostics until its task is joined.
class WGodotCheckBudget {
	const int limit;
	std::atomic<int> errors{ 0 };

public:
	explicit WGodotCheckBudget(int p_limit = 0) :
			limit(p_limit) {}

	bool exhausted() const {
		return limit > 0 && errors.load(std::memory_order_relaxed) >= limit;
	}

	bool claim_error() {
		if (limit == 0) {
			return true;
		}
		int count = errors.load(std::memory_order_relaxed);
		while (count < limit) {
			if (errors.compare_exchange_weak(count, count + 1, std::memory_order_relaxed)) {
				return true;
			}
		}
		return false;
	}

	int get_limit() const { return limit; }
};
