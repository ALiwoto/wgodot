// wgodot-changes::file
#include "wgodot_native_connections.h"

#include "core/object/wgodot_native_lifetime.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

namespace WGodotNative {
namespace {
struct Registry {
	Mutex mutex;
	HashMap<ObjectID, Vector<std::weak_ptr<NativeConnections::Lease>>> receivers;
};
Registry &registry() {
	static Registry value;
	return value;
}
void object_deleted(ObjectID p_receiver) {
	Vector<std::weak_ptr<NativeConnections::Lease>> connections;
	{
		MutexLock lock(registry().mutex);
		if (auto *found = registry().receivers.getptr(p_receiver)) {
			connections = std::move(*found);
			registry().receivers.erase(p_receiver);
		}
	}
	for (const auto &weak : connections) {
		if (auto connection = weak.lock()) {
			connection->disconnect();
		}
	}
}
} // namespace

std::shared_ptr<NativeConnections::Lease> NativeConnections::watch(ObjectID p_receiver, std::function<void()> p_disconnect) {
	if (p_receiver.is_null()) {
		return {};
	}
	auto lease = std::make_shared<Lease>();
	lease->disconnect = std::move(p_disconnect);
	MutexLock lock(registry().mutex);
	auto &connections = registry().receivers[p_receiver];
	// A disconnected connection can leave a weak entry until the next connect.
	// Pruning here bounds storage by active connections, even for long-lived nodes.
	for (int i = connections.size() - 1; i >= 0; i--) {
		if (connections[i].expired()) {
			connections.remove_at(i);
		}
	}
	connections.push_back(lease);
	return lease;
}

void NativeConnections::initialize() {
	WGodotNativeLifetime::object_deleted = &object_deleted;
}

void NativeConnections::clear() {
	HashMap<ObjectID, Vector<std::weak_ptr<Lease>>> receivers;
	{
		MutexLock lock(registry().mutex);
		receivers = std::move(registry().receivers);
	}
	for (const auto &receiver : receivers) {
		for (const auto &weak : receiver.value) {
			if (auto connection = weak.lock()) {
				connection->disconnect();
			}
		}
	}
	WGodotNativeLifetime::object_deleted = nullptr;
}
} // namespace WGodotNative
