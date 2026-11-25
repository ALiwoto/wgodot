// wgodot-changes::file
#include "vm_cache.h"

#include "partial_classes.h"
#include "vm_cache_bindings.h"
#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/debugger/engine_debugger.h"
#include "core/debugger/script_debugger.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/marshalls.h"
#include "core/io/wgodot_build_fingerprint.h"
#include "core/io/wgodot_file_stamp.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/profiling/wgodot_startup_profile.h"
#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_cache.h"
#include "modules/gdscript/gdscript_parser.h"

namespace {
constexpr uint32_t MAGIC = 0x4D564757; // WGVM
constexpr int FORMAT_VERSION = 5;

struct Header {
	Dictionary data;
	uint64_t payload_offset = 0;
	bool valid = false;
};

struct LoadState {
	int depth = 0;
	HashMap<String, String> stamps;
	HashMap<String, Header> headers;
	HashSet<String> verified;
};
thread_local LoadState load_state;

Mutex context_mutex;
String context_source_stamp;
String context_class_stamp;
String context_id;
bool context_tracks_locals = false;

String directory() {
	const String &build = WGodotBuildFingerprint::get();
	if (build.is_empty()) {
		return String();
	}
	return ProjectSettings::get_singleton()->get_project_data_path().path_join("vm_cache").path_join(build);
}

String cache_path(const String &p_source) {
	return directory().path_join(p_source.trim_prefix("res://") + ".gvm");
}

String stamp(const String &p_source) {
	if (const String *cached = load_state.stamps.getptr(p_source)) {
		return *cached;
	}
	const String result = WGodotFileStamp::get(p_source);
	load_state.stamps.insert(p_source, result);
	return result;
}

String temporary_path(const String &p_path) {
	return p_path + "." + itos(OS::get_singleton()->get_process_id()) + "." + uitos(Thread::get_caller_id()) + ".tmp";
}

String context() {
	MutexLock lock(context_mutex);
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String source_stamp = WGodotFileStamp::get("res://project.godot");
	const String class_stamp = WGodotFileStamp::get(settings->get_global_class_list_path());
	const bool track_locals = GDScriptLanguage::get_singleton()->should_track_locals();
	if (!context_id.is_empty() && context_source_stamp == source_stamp && context_class_stamp == class_stamp && context_tracks_locals == track_locals) {
		return context_id;
	}
	// Compare registry metadata once when its backing file changes, without
	// hashing scripts or invalidating everything on an identical registry save.
	Dictionary configuration;
	configuration["project"] = source_stamp;
	configuration["classes"] = settings->get_global_class_list();
	configuration["track_locals"] = track_locals;
	configuration["enforce_resource_paths"] = GLOBAL_GET("wgodot/gdscript/enforce_resource_paths");
	configuration["extensions"] = WGodotFileStamp::get(settings->get_project_data_path().path_join("extension_list.cfg"));
	const String path = directory().path_join("context.bin");
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	Dictionary previous;
	if (file.is_valid()) {
		previous = file->get_var(false);
		file.unref();
	}
	if (previous.get("configuration", Variant()) == Variant(configuration)) {
		context_id = previous.get("id", String());
	} else {
		context_id = itos(OS::get_singleton()->get_unix_time()) + "-" + uitos(OS::get_singleton()->get_ticks_usec()) + "-" + itos(OS::get_singleton()->get_process_id());
		if (DirAccess::make_dir_recursive_absolute(directory()) != OK) {
			return String();
		}
		const String temporary = temporary_path(path);
		file = FileAccess::open(temporary, FileAccess::WRITE);
		if (file.is_null()) {
			return String();
		}
		file->store_var(Dictionary{ { "configuration", configuration }, { "id", context_id } });
		file.unref();
		if (DirAccess::rename_absolute(temporary, path) != OK) {
			return String();
		}
	}
	context_source_stamp = source_stamp;
	context_class_stamp = class_stamp;
	context_tracks_locals = track_locals;
	return context_id;
}

Header header(const String &p_source, const String &p_context) {
	if (const Header *cached = load_state.headers.getptr(p_source)) {
		return *cached;
	}
	Header result;
	Ref<FileAccess> file = FileAccess::open(cache_path(p_source), FileAccess::READ);
	if (file.is_valid() && file->get_32() == MAGIC) {
		const uint32_t size = file->get_32();
		const uint32_t checksum = file->get_32();
		if (file->get_length() < 12 || size > file->get_length() - 12) {
			load_state.headers.insert(p_source, result);
			return result;
		}
		Vector<uint8_t> encoded;
		encoded.resize(size);
		Variant data;
		if (file->get_buffer(encoded.ptrw(), size) == size &&
				hash_murmur3_buffer(encoded.ptr(), size) == checksum &&
				decode_variant(data, encoded.ptr(), size, nullptr, false) == OK && data.get_type() == Variant::DICTIONARY) {
			result.data = data;
			result.payload_offset = file->get_position();
			result.valid = int(result.data.get("version", 0)) == FORMAT_VERSION &&
					String(result.data.get("context", String())) == p_context &&
					String(result.data.get("source", String())) == p_source &&
					String(result.data.get("stamp", String())) == stamp(p_source) &&
					stamp(p_source) != "<missing>";
		}
	}
	load_state.headers.insert(p_source, result);
	return result;
}

bool validate(const String &p_source, const String &p_context) {
	if (load_state.verified.has(p_source)) {
		return true;
	}
	Vector<String> pending;
	HashSet<String> visited;
	pending.push_back(p_source);
	// Validate the complete reachable graph before marking any cyclic component
	// valid. A back edge must not conceal a changed dependency later in the walk.
	for (int index = 0; index < pending.size(); ++index) {
		const String path = pending[index];
		if (visited.has(path) || load_state.verified.has(path)) {
			continue;
		}
		visited.insert(path);
		const Header entry = header(path, p_context);
		if (!entry.valid) {
			return false;
		}
		// New/removed partial declarations change the combined program even when
		// every previously recorded source file is unchanged. The folder index
		// also notices an existing ordinary script becoming a partial declaration.
		const PackedStringArray parts = entry.data["parts"];
		if (parts != WGodotPartialClasses::paths(path)) {
			return false;
		}
		const Array dependencies = entry.data["dependencies"];
		for (const Array dependency : dependencies) {
			const String dependency_path = dependency[0];
			if (stamp(dependency_path) != String(dependency[1]) || stamp(dependency_path) == "<missing>") {
				return false;
			}
			if (dependency[2]) {
				pending.push_back(dependency_path);
			}
		}
	}
	for (const String &path : visited) {
		load_state.verified.insert(path);
	}
	return true;
}
} // namespace

WGodotGDScriptVMCache::LoadScope::LoadScope() {
	++load_state.depth;
}

WGodotGDScriptVMCache::LoadScope::~LoadScope() {
	if (--load_state.depth == 0) {
		load_state.stamps.clear();
		load_state.headers.clear();
		load_state.verified.clear();
	}
}

bool WGodotGDScriptVMCache::prepare(GDScript *p_script) {
	if (!GLOBAL_GET_CACHED(bool, "wgodot/gdscript/vm_cache") || p_script->wgodot_analysis_only ||
			p_script->wgodot_vm_source_stamp.is_empty() || directory().is_empty()) {
		return false;
	}
	WGodotStartupProfile::Scope profile("GDScript VM cache validate", p_script->path);
	const String current_context = context();
	if (current_context.is_empty() || !validate(p_script->path, current_context)) {
		return false;
	}
	const Header entry = header(p_script->path, current_context);
	const int64_t size = entry.data["payload_size"];
	if (size <= 0) {
		return false;
	}
	Ref<FileAccess> file = FileAccess::open(cache_path(p_script->path), FileAccess::READ);
	if (file.is_null() || file->get_length() != entry.payload_offset + uint64_t(size)) {
		return false;
	}
	file->seek(entry.payload_offset);
	Vector<uint8_t> payload;
	payload.resize(size);
	if (file->get_buffer(payload.ptrw(), size) != uint64_t(size)) {
		return false;
	}
	// One fast checksum per cache read detects damaged/partial files. It is not
	// used for source identity, dependency checks or per-frame work.
	if (hash_murmur3_buffer(payload.ptr(), payload.size()) != uint32_t(int64_t(entry.data["payload_checksum"]))) {
		return false;
	}
	Variant snapshot;
	if (decode_variant(snapshot, payload.ptr(), payload.size(), nullptr, false) != OK || snapshot.get_type() != Variant::DICTIONARY) {
		return false;
	}
	payload.clear();
	p_script->wgodot_vm_snapshot = snapshot;
	if (!decode(p_script, p_script->wgodot_vm_snapshot, true)) {
		p_script->wgodot_vm_snapshot.clear();
		return false;
	}
	for (const Array dependency : Array(entry.data["dependencies"])) {
		p_script->wgodot_vm_dependencies.insert(dependency[0], dependency[1]);
		if (dependency[2]) {
			GDScriptCache::singleton->parser_inverse_dependencies[String(dependency[0])].insert(p_script->path);
		}
	}
	return true;
}

bool WGodotGDScriptVMCache::restore(GDScript *p_script, Error &r_error) {
	if (p_script->wgodot_vm_snapshot.is_empty() || p_script->valid || p_script->instances.first()) {
		return false;
	}
	WGodotStartupProfile::Scope profile("GDScript VM cache restore", p_script->path);
	Dictionary snapshot = p_script->wgodot_vm_snapshot;
	p_script->wgodot_vm_snapshot = Dictionary();
	if (!decode(p_script, snapshot, false)) {
		// The ordinary compiler rebuilds the existing class objects on a miss.
		p_script->valid = false;
		return false;
	}
	const bool retain_static = snapshot["retain_static"];
#ifdef DEBUG_ENABLED
	const Array warnings = snapshot["warnings"];
#endif
	// Dependency restoration can recurse through an entire application. The VM
	// owns the decoded values now; do not retain every caller's serialized graph.
	snapshot = Dictionary();
	for (const auto &dependency : p_script->wgodot_vm_dependencies) {
		if (dependency.key.get_extension() == "gd" && dependency.key != p_script->path) {
			Error error = OK;
			GDScriptCache::get_full_script(dependency.key, error, p_script->path);
			if (error != OK) {
				r_error = error;
				return true;
			}
		}
	}
	r_error = GDScriptCache::finish_compiling(p_script->path);
	if (r_error != OK) {
		return true;
	}
	// Preserve the compiler's static lifetime policy, including inner classes.
	if (retain_static) {
		GDScriptCache::add_static_script(Ref<GDScript>(p_script));
	}
#ifdef DEBUG_ENABLED
	if (EngineDebugger::is_active()) {
		for (const Array warning : warnings) {
			Vector<ScriptLanguage::StackInfo> stack;
			EngineDebugger::get_script_debugger()->send_error("GDScript::reload", warning[0], warning[1], warning[2], warning[3], false, ERR_HANDLER_WARNING, stack);
		}
	}
#endif
	if (ScriptServer::is_scripting_enabled() || p_script->tool) {
		r_error = p_script->_static_init();
	}
	return true;
}

void WGodotGDScriptVMCache::capture_dependencies(GDScript *p_script, const HashSet<String> &p_paths) {
	for (const String &path : p_paths) {
		String source_stamp = stamp(path);
		Ref<GDScript> dependency = GDScriptCache::get_cached_script(path);
		if (dependency.is_valid()) {
			source_stamp = dependency->wgodot_vm_source_stamp;
		}
		if (GDScriptParserRef **parser = GDScriptCache::singleton->parser_map.getptr(path)) {
			const String &parsed_stamp = (*parser)->wgodot_vm_source_stamp;
			if (dependency.is_null() || source_stamp == parsed_stamp) {
				source_stamp = parsed_stamp;
			} else {
				source_stamp = String(); // Parser and compiled declaration used different files.
			}
		}
		p_script->wgodot_vm_dependencies.insert(path, source_stamp);
	}
}

void WGodotGDScriptVMCache::store(GDScript *p_script, const GDScriptParser &p_parser) {
	if (!GLOBAL_GET_CACHED(bool, "wgodot/gdscript/vm_cache") || p_script->wgodot_analysis_only ||
			!p_script->valid || p_script->wgodot_vm_source_stamp.is_empty() || directory().is_empty() ||
			WGodotFileStamp::get(p_script->path) != p_script->wgodot_vm_source_stamp) {
		return;
	}
	WGodotStartupProfile::Scope profile("GDScript VM cache store", p_script->path);
	const String current_context = context();
	if (current_context.is_empty()) {
		return;
	}
	Dictionary snapshot = encode(p_script);
	if (!snapshot.is_empty()) {
		bool has_static = false;
		Vector<GDScript *> classes;
		classes.push_back(p_script);
		for (int index = 0; index < classes.size(); ++index) {
			has_static |= classes[index]->static_initializer != nullptr;
			for (const auto &child : classes[index]->subclasses) {
				classes.push_back(child.value.ptr());
			}
		}
		snapshot["retain_static"] = has_static && !p_parser.get_tree()->annotated_static_unload;
		Array warnings;
#ifdef DEBUG_ENABLED
		for (const GDScriptWarning &warning : p_parser.get_warnings()) {
			const auto location = p_parser.wgodot_source_location(warning.start_line);
			warnings.push_back(Array{ location.path, location.line, warning.get_name(), warning.get_message() });
		}
#endif
		snapshot["warnings"] = warnings;
	}
	Array dependencies;
	const Vector<String> parts = WGodotPartialClasses::paths(p_script->path);
	for (const auto &dependency : p_script->wgodot_vm_dependencies) {
		if (WGodotFileStamp::get(dependency.key) != dependency.value) {
			return; // A file changed while this script was compiling.
		}
		dependencies.push_back(Array{ dependency.key, dependency.value, dependency.key.get_extension() == "gd" && !parts.has(dependency.key) });
	}
	for (const String &part : parts) {
		if (part != p_script->path && !p_script->wgodot_vm_dependencies.has(part)) {
			const String source_stamp = WGodotPartialClasses::source_stamp(part);
			if (source_stamp.is_empty() || source_stamp != WGodotFileStamp::get(part)) {
				return;
			}
			dependencies.push_back(Array{ part, source_stamp, false });
		}
	}
	Vector<uint8_t> payload;
	int size = 0;
	if (!snapshot.is_empty() && encode_variant(snapshot, nullptr, size, false) == OK) {
		payload.resize(size);
		if (encode_variant(snapshot, payload.ptrw(), size, false) != OK) {
			payload.clear();
		}
	}
	Dictionary metadata;
	metadata["version"] = FORMAT_VERSION;
	metadata["context"] = current_context;
	metadata["source"] = p_script->path;
	metadata["stamp"] = p_script->wgodot_vm_source_stamp;
	metadata["dependencies"] = dependencies;
	metadata["parts"] = parts;
	metadata["payload_size"] = payload.size();
	metadata["payload_checksum"] = payload.is_empty() ? 0 : int64_t(hash_murmur3_buffer(payload.ptr(), payload.size()));
	const String path = cache_path(p_script->path);
	if (DirAccess::make_dir_recursive_absolute(path.get_base_dir()) != OK) {
		return;
	}
	const String temporary = temporary_path(path);
	int header_size = 0;
	if (encode_variant(metadata, nullptr, header_size, false) != OK) {
		return;
	}
	Vector<uint8_t> encoded_header;
	encoded_header.resize(header_size);
	if (encode_variant(metadata, encoded_header.ptrw(), header_size, false) != OK) {
		return;
	}
	Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE);
	if (file.is_null()) {
		return;
	}
	file->store_32(MAGIC);
	file->store_32(encoded_header.size());
	file->store_32(hash_murmur3_buffer(encoded_header.ptr(), encoded_header.size()));
	file->store_buffer(encoded_header.ptr(), encoded_header.size());
	file->store_buffer(payload.ptr(), payload.size());
	const Error error = file->get_error();
	file.unref();
	if (error == OK && DirAccess::rename_absolute(temporary, path) == OK) {
		load_state.headers.erase(p_script->path);
		load_state.verified.erase(p_script->path);
	}
}

void WGodotGDScriptVMCache::clear() {
	MutexLock lock(context_mutex);
	context_id = String();
	context_source_stamp = String();
	context_class_stamp = String();
	WGodotVMCacheBindings::clear();
}
