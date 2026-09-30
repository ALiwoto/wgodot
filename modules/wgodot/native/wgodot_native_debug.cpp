// wgodot-changes::file
#include "wgodot_native_debug.h"

#ifdef WGODOT_NATIVE_TRACE_ACTIVE
#include "core/object/object.h"
#include "core/object/wgodot_native_lifetime.h"
#include "core/os/wgodot_native_allocation.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace WGodotNative {
namespace {
struct ObjectRecord {
	std::string class_name;
	DebugSource context;
	bool deleting = false;
	const char *cpp_type = nullptr;
};

struct Registry {
	std::mutex mutex;
	std::unordered_map<uint64_t, ObjectRecord> objects;
	std::unordered_map<std::string, DebugSource> classes;
};

Registry &registry() {
	// Retain plain metadata through ObjectDB::cleanup, after module shutdown.
	// No Object, Ref, Variant, or StringName is retained by these records.
	static Registry value;
	return value;
}

String describe_source(DebugSource p_source) {
	if (!p_source.file) {
		return "<engine/native code>";
	}
	return String(p_source.file) + ":" + itos(p_source.line) + " in " + p_source.function;
}

void object_initialized(Object *p_object) {
	ObjectRecord record{ p_object->get_class().utf8().get_data(), DebugFrame::current_source() };
	std::lock_guard<std::mutex> lock(registry().mutex);
	registry().objects.insert_or_assign(uint64_t(p_object->get_instance_id()), std::move(record));
}

void object_predelete(ObjectID p_id) {
	std::lock_guard<std::mutex> lock(registry().mutex);
	const auto found = registry().objects.find(uint64_t(p_id));
	if (found != registry().objects.end()) {
		found->second.deleting = true;
	}
}

void object_allocated(Object *p_object, const char *p_cpp_type) {
	std::lock_guard<std::mutex> lock(registry().mutex);
	const auto found = registry().objects.find(uint64_t(p_object->get_instance_id()));
	if (found != registry().objects.end()) {
		found->second.cpp_type = p_cpp_type;
	}
}

void object_removed(ObjectID p_id) {
	std::lock_guard<std::mutex> lock(registry().mutex);
	registry().objects.erase(uint64_t(p_id));
}

String describe_object(ObjectID p_id) {
	std::lock_guard<std::mutex> lock(registry().mutex);
	const auto found = registry().objects.find(uint64_t(p_id));
	if (found == registry().objects.end()) {
		return " - Native trace: object predates tracing or was not postinitialized";
	}
	const ObjectRecord &record = found->second;
	String result = " - Original class: " + String(record.class_name.c_str());
	if (record.cpp_type) {
		result += "\n    C++ allocation type: " + String(record.cpp_type);
	}
	const auto source = registry().classes.find(record.class_name);
	if (source != registry().classes.end()) {
		result += "\n    Script class: " + describe_source(source->second);
	}
	result += "\n    Allocation context: " + describe_source(record.context);
	result += String("\n    Accepted predelete: ") + (record.deleting ? "yes" : "no");
	return result;
}
} // namespace

thread_local const DebugFrame *DebugFrame::current = nullptr;

DebugFrame::DebugFrame(DebugSource p_source, const char *p_operation) :
		previous(current), source(p_source), operation(p_operation) {
	current = this;
}

DebugFrame::~DebugFrame() {
	current = previous;
}

DebugSource DebugFrame::current_source() {
	for (const DebugFrame *frame = current; frame; frame = frame->previous) {
		if (frame->source.file) {
			return frame->source;
		}
	}
	return {};
}

void DebugFrame::report_callback_failure(const char *p_reason, DebugSource p_target, ObjectID p_owner, int p_provided, int p_minimum, int p_maximum) {
	String message = String("Invalid native callback invocation: ") + p_reason;
	message += "\n    Target: " + describe_source(p_target);
	message += "\n    Owner ID: " + uitos(uint64_t(p_owner));
	if (p_owner.is_valid()) {
		message += ObjectDB::get_instance(p_owner) ? " (alive)" : " (freed)";
	}
	message += "\n    Arguments: supplied " + itos(p_provided) + ", required " + itos(p_minimum) + ".." + itos(p_maximum);
	for (const DebugFrame *frame = current; frame; frame = frame->previous) {
		message += "\n    " + String(frame->operation) + " " + describe_source(frame->source);
	}
	ERR_PRINT(message);
}

void NativeDebug::initialize() {
	WGodotNativeAllocation::object_allocated = &object_allocated;
	WGodotNativeLifetime::object_initialized = &object_initialized;
	WGodotNativeLifetime::object_predelete = &object_predelete;
	WGodotNativeLifetime::object_removed = &object_removed;
	WGodotNativeLifetime::describe_object = &describe_object;
}

void NativeDebug::register_class(const char *p_cpp_name, DebugSource p_source) {
	std::lock_guard<std::mutex> lock(registry().mutex);
	registry().classes.insert_or_assign(p_cpp_name, p_source);
}
} // namespace WGodotNative
#endif
