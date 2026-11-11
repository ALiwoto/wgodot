// wgodot-changes::file
#include "wgodot_global_resources.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/resource_loader.h"
#include "core/os/mutex.h"
#include "core/variant/variant_internal.h"

namespace WGlobalResources {
namespace {
struct Entry {
	String path;
	Ref<Resource> resource;
	HashMap<StringName, Variant> members;
};
Mutex mutex;
HashMap<StringName, Entry> entries;
HashMap<const void *, Reference> identities;
bool configured = false;
Error configuration_error = OK;

const void *identity(const Variant &p_value) {
	// Native property getters can return a new packed handle over the same COW
	// allocation. Its backing address preserves sharing across that boundary.
	switch (p_value.get_type()) {
#define GLOBAL_PACKED_ID(m_kind, m_accessor) \
	case Variant::m_kind: { \
		const auto *array = VariantInternal::m_accessor(&p_value); \
		return array->is_empty() ? static_cast<const void *>(array) : array->ptr(); \
	}
		GLOBAL_PACKED_ID(PACKED_BYTE_ARRAY, get_byte_array)
		GLOBAL_PACKED_ID(PACKED_INT32_ARRAY, get_int32_array)
		GLOBAL_PACKED_ID(PACKED_INT64_ARRAY, get_int64_array)
		GLOBAL_PACKED_ID(PACKED_FLOAT32_ARRAY, get_float32_array)
		GLOBAL_PACKED_ID(PACKED_FLOAT64_ARRAY, get_float64_array)
		GLOBAL_PACKED_ID(PACKED_STRING_ARRAY, get_string_array)
		GLOBAL_PACKED_ID(PACKED_VECTOR2_ARRAY, get_vector2_array)
		GLOBAL_PACKED_ID(PACKED_VECTOR3_ARRAY, get_vector3_array)
		GLOBAL_PACKED_ID(PACKED_COLOR_ARRAY, get_color_array)
		GLOBAL_PACKED_ID(PACKED_VECTOR4_ARRAY, get_vector4_array)
#undef GLOBAL_PACKED_ID
		default:
			break;
	}
	if (p_value.get_type() == Variant::ARRAY) {
		return Array(p_value).id();
	}
	if (p_value.get_type() == Variant::DICTIONARY) {
		return Dictionary(p_value).id();
	}
	if (p_value.get_type() == Variant::OBJECT) {
		return Object::cast_to<Resource>(p_value);
	}
	return nullptr;
}

Error scan(const String &p_root, const String &p_directory) {
	Ref<DirAccess> directory = DirAccess::open(p_directory);
	ERR_FAIL_COND_V_MSG(directory.is_null(), ERR_CANT_OPEN, "Cannot open global resource directory: " + p_directory);
	Vector<String> names;
	directory->list_dir_begin();
	for (String name = directory->get_next(); !name.is_empty(); name = directory->get_next()) {
		if (!name.begins_with(".")) {
			names.push_back(name);
		}
	}
	directory->list_dir_end();
	names.sort();
	for (const String &name : names) {
		const String path = p_directory.path_join(name);
		if (directory->dir_exists(name)) {
			Error error = scan(p_root, path);
			if (error != OK) {
				return error;
			}
		} else if (name.get_extension() == "tres" || name.get_extension() == "res") {
			const StringName key = path.trim_prefix(p_root + "/").get_basename();
			ERR_FAIL_COND_V_MSG(entries.has(key), ERR_ALREADY_EXISTS, "Duplicate global resource name: " + String(key));
			Entry entry;
			entry.path = path;
			entries.insert(key, entry);
		}
	}
	return OK;
}

Error configure() {
	MutexLock lock(mutex);
	if (configured) {
		return configuration_error;
	}
	configured = true;
	const PackedStringArray directories = GLOBAL_GET("wgodot/resources/global_directories");
	for (const String &directory : directories) {
		if (!directory.begins_with("res://") || directory.simplify_path() != directory || directory.ends_with("/")) {
			configuration_error = ERR_INVALID_PARAMETER;
			ERR_FAIL_V_MSG(configuration_error, "Global resource directories must be canonical res:// paths without a trailing slash: " + directory);
		}
		configuration_error = scan(directory, directory);
		if (configuration_error != OK) {
			return configuration_error;
		}
	}
	return OK;
}

Error load(const StringName &p_name, String &r_error) {
	Error error = configure();
	if (error != OK) {
		r_error = "Invalid global resource configuration.";
		return error;
	}
	String path;
	{
		MutexLock lock(mutex);
		const Entry *entry = entries.getptr(p_name);
		if (!entry) {
			r_error = "Unknown global resource: " + String(p_name);
			return ERR_DOES_NOT_EXIST;
		}
		if (entry->resource.is_valid()) {
			return OK;
		}
		path = entry->path;
	}
	// ResourceLoader coordinates concurrent loads and detects dependency cycles.
	// Do not hold the registry mutex across a load: dependencies can use it too.
	Ref<Resource> resource = ResourceLoader::load(path, String(), ResourceFormatLoader::CACHE_MODE_REUSE, &error);
	if (resource.is_null()) {
		r_error = "Cannot load global resource: " + String(p_name) + " (" + path + ")";
		return error == OK ? ERR_CANT_OPEN : error;
	}
	MutexLock lock(mutex);
	Entry &entry = entries[p_name];
	if (entry.resource.is_valid()) {
		return OK;
	}
	entry.resource = resource;
	identities.insert(resource.ptr(), { p_name, StringName() });
	List<PropertyInfo> properties;
	resource->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (!(property.usage & PROPERTY_USAGE_STORAGE) || property.name == "script") {
			continue;
		}
		const Variant member = resource->get(property.name);
		const void *key = identity(member);
		if (key) {
			entry.members.insert(property.name, member);
			if (!identities.has(key)) {
				identities.insert(key, { p_name, property.name });
			}
		}
	}
	return OK;
}
} // namespace

void register_settings() {
	GLOBAL_DEF(PropertyInfo(Variant::PACKED_STRING_ARRAY, "wgodot/resources/global_directories", PROPERTY_HINT_TYPE_STRING,
					   vformat("%d/%d:", Variant::STRING, PROPERTY_HINT_DIR)),
			PackedStringArray());
}

HashMap<StringName, String> paths() {
	HashMap<StringName, String> result;
	ERR_FAIL_COND_V(configure() != OK, result);
	MutexLock lock(mutex);
	for (const auto &entry : entries) {
		result.insert(entry.key, entry.value.path);
	}
	return result;
}

void set_manifest(const HashMap<StringName, String> &p_paths) {
	MutexLock lock(mutex);
	entries.clear();
	identities.clear();
	configured = true;
	configuration_error = OK;
	for (const auto &path : p_paths) {
		Entry entry;
		entry.path = path.value;
		entries.insert(path.key, entry);
	}
}

Error start() {
	Error error = configure();
	if (error != OK) {
		return error;
	}
	Vector<String> names;
	for (const auto &path : paths()) {
		names.push_back(path.key);
	}
	names.sort();
	for (const String &name : names) {
		String message;
		error = load(name, message);
		ERR_FAIL_COND_V_MSG(error != OK, error, message);
	}
	return OK;
}

void clear() {
	MutexLock lock(mutex);
	identities.clear();
	entries.clear();
	configured = false;
	configuration_error = OK;
}

Error resolve(const StringName &p_name, const StringName &p_member, Variant &r_value, String &r_error) {
	Error error = load(p_name, r_error);
	if (error != OK) {
		return error;
	}
	MutexLock lock(mutex);
	const Entry &entry = entries[p_name];
	if (p_member == StringName()) {
		r_value = entry.resource;
		return OK;
	}
	const Variant *member = entry.members.getptr(p_member);
	if (!member) {
		r_error = "GlobalResource(\"" + String(p_name) + "\")." + String(p_member) + " must name a stored array, dictionary or resource.";
		return ERR_INVALID_DATA;
	}
	r_value = *member;
	return OK;
}

Variant value(const StringName &p_name, const StringName &p_member) {
	Variant result;
	String error;
	ERR_FAIL_COND_V_MSG(resolve(p_name, p_member, result, error) != OK, Variant(), error);
	return result;
}

bool reference(const Variant &p_value, Reference &r_reference, const String &p_exclude) {
	const void *key = identity(p_value);
	if (!key) {
		return false;
	}
	MutexLock lock(mutex);
	const Reference *found = identities.getptr(key);
	if (!found || entries[found->resource].path == p_exclude) {
		return false;
	}
	r_reference = *found;
	return true;
}

String Reference::text() const {
	return "GlobalResource(\"" + String(resource).c_escape() + "\")" + (member == StringName() ? String() : "." + String(member));
}

Variant snapshot(const Variant &p_value) {
	Reference global;
	if (reference(p_value, global)) {
		return p_value;
	}
	if (p_value.get_type() == Variant::ARRAY) {
		const Array source = p_value;
		Array copy = source.duplicate(false);
		for (int i = 0; i < source.size(); i++) {
			copy[i] = snapshot(source[i]);
		}
		return copy;
	}
	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary source = p_value;
		Dictionary copy = source.duplicate(false);
		for (const auto &entry : source) {
			copy[entry.key] = snapshot(entry.value);
		}
		return copy;
	}
	return p_value;
}

thread_local SaveScope *SaveScope::current = nullptr;
SaveScope::SaveScope(const Ref<Resource> &p_resource) : previous(current), source(p_resource->get_path()) {
	current = this;
}
SaveScope::~SaveScope() {
	current = previous;
}
bool SaveScope::reference(const Variant &p_value, Reference &r_reference) {
	return current && WGlobalResources::reference(p_value, r_reference, current->source);
}

Error parse(VariantParser::Stream *p_stream, int &r_line, String &r_error, Variant &r_value, bool p_syntax_only) {
	VariantParser::Token token;
	const VariantParser::TokenType expected[] = { VariantParser::TK_PARENTHESIS_OPEN, VariantParser::TK_STRING, VariantParser::TK_PARENTHESIS_CLOSE };
	StringName name;
	for (auto type : expected) {
		Error error = VariantParser::get_token(p_stream, token, r_line, r_error);
		if (error != OK) {
			return error;
		}
		if (token.type != type) {
			r_error = "Expected GlobalResource(\"name\") or GlobalResource(\"name\").member.";
			return ERR_PARSE_ERROR;
		}
		if (type == VariantParser::TK_STRING) {
			name = String(token.value);
		}
	}
	char32_t next = p_stream->get_char();
	while (next == ' ' || next == '\t' || next == '\r' || next == '\n') {
		if (next == '\n') {
			r_line++;
		}
		next = p_stream->get_char();
	}
	StringName member;
	if (next == '.') {
		Error error = VariantParser::get_token(p_stream, token, r_line, r_error);
		if (error != OK) {
			return error;
		}
		if (token.type != VariantParser::TK_IDENTIFIER) {
			r_error = "Expected a global resource member name after '.'.";
			return ERR_PARSE_ERROR;
		}
		member = String(token.value);
	} else {
		p_stream->saved = next;
	}
	return p_syntax_only ? OK : resolve(name, member, r_value, r_error);
}
} // namespace WGlobalResources
