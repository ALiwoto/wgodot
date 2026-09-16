// wgodot-changes::file
#pragma once

#include "core/object/object_id.h"

#include <functional>
#include <memory>

namespace WGodotNative {
class NativeConnections {
public:
	struct Lease {
		std::function<void()> disconnect;
	};
	static std::shared_ptr<Lease> watch(ObjectID p_receiver, std::function<void()> p_disconnect);
	static void initialize();
	static void clear();
};
} // namespace WGodotNative
