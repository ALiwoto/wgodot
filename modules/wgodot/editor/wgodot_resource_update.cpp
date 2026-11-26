// wgodot-changes::file
#include "wgodot_resource_update.h"

#include "wgodot_native_build_lock.h"
#include "wgodot_resource_document.h"
#include "wgodot_resource_pack.h"
#include "wgodot_resource_snapshot.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/io/wgodot_file_stamp.h"
#include "core/os/os.h"

#include "modules/wgodot/codecs/native_payload.h"

namespace {
Error read_manifest(const String &p_path, Dictionary &r_manifest) {
	JSON json;
	RETURN_IF_ERROR(json.parse_preserving_integers(FileAccess::get_file_as_string(p_path)));
	ERR_FAIL_COND_V(json.get_data().get_type() != Variant::DICTIONARY, ERR_FILE_CORRUPT);
	r_manifest = json.get_data();
	return OK;
}

Error write_manifest(const String &p_path, const Dictionary &p_manifest) {
	Error error;
	const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &error);
	RETURN_IF_ERROR(error);
	file->store_string(JSON::stringify(p_manifest, "\t", true) + "\n");
	file->flush();
	return file->get_error();
}

Error unchanged(const Dictionary &p_sources, String &r_error) {
	for (const auto &entry : p_sources) {
		if (WGodotFileStamp::get(entry.key) != String(entry.value)) {
			r_error = "Compiled input changed; run normal generation/packaging: " + String(entry.key);
			return ERR_INVALID_DATA;
		}
	}
	return OK;
}

// The journal contains only generated filenames and a completed replacement
// pack. A failed rename (for example an open pack on Windows) can be retried
// without losing the old pack or blessing partially published native outputs.
Error publish(const String &p_directory, const String &p_pack, String &r_error) {
	const String staging = p_directory.path_join(".resource-update");
	const String journal_path = staging.path_join("transaction.json");
	Dictionary journal;
	RETURN_IF_ERROR(read_manifest(journal_path, journal));
	if (String(journal["pack"]) != p_pack) {
		r_error = "Pending resource update belongs to a different pack: " + String(journal["pack"]);
		return ERR_INVALID_DATA;
	}
	const String replacement = p_pack + ".resource-update";
	if (FileAccess::exists(replacement) && WGodotFileStamp::get(p_pack) != String(journal["pack_revision"])) {
		r_error = "Pack changed while a resource update was pending. Run normal packaging.";
		return ERR_BUSY;
	}
	const PackedStringArray files = journal["files"];
	for (const String &name : files) {
		ERR_FAIL_COND_V(name != name.get_file() || !name.begins_with("resource_") || !name.ends_with(".wgp"), ERR_INVALID_DATA);
		const String path = staging.path_join(name);
		if (FileAccess::exists(path)) {
			RETURN_IF_ERROR(DirAccess::rename_absolute(path, p_directory.path_join(name)));
		}
	}
	if (FileAccess::exists(replacement)) {
		const Error error = DirAccess::rename_absolute(replacement, p_pack);
		if (error != OK) {
			r_error = "Cannot replace the pack. Close processes using it, then retry -UpdateResources.";
			return error;
		}
	} else {
		WGodotResourcePack pack;
		RETURN_IF_ERROR(pack.open(p_pack));
		Dictionary expected;
		RETURN_IF_ERROR(read_manifest(staging.path_join("main_game.json"), expected));
		if (pack.read(WGodotResourceSnapshot::PACK_IDENTITY) != WGodotResourceSnapshot::pack_identity(expected)) {
			r_error = "Pending update has no matching completed pack; run normal packaging.";
			return ERR_INVALID_DATA;
		}
	}
	Dictionary manifest;
	RETURN_IF_ERROR(read_manifest(staging.path_join("main_game.json"), manifest));
	const String manifest_temporary = p_directory.path_join("main_game.json.resource-update");
	RETURN_IF_ERROR(write_manifest(manifest_temporary, manifest));
	RETURN_IF_ERROR(DirAccess::rename_absolute(manifest_temporary, p_directory.path_join("main_game.json")));
	return DirAccess::remove_absolute(journal_path);
}

Error update(const String &p_directory, const String &p_pack, String &r_error) {
	WGodotNativeBuildLock lock;
	if (lock.acquire(p_directory) != OK) {
		r_error = "Another native generation or resource update owns this module. Retry after it completes.";
		return ERR_BUSY;
	}
	WGodotNativeBuildLock pack_lock;
	if (pack_lock.acquire(p_pack.get_base_dir()) != OK) {
		r_error = "Another build is publishing in this pack directory.";
		return ERR_BUSY;
	}
	const uint64_t start = OS::get_singleton()->get_ticks_msec();
	const String staging = p_directory.path_join(".resource-update");
	const bool recovered = FileAccess::exists(staging.path_join("transaction.json"));
	if (recovered) {
		RETURN_IF_ERROR(publish(p_directory, p_pack, r_error));
		print_line("Recovered the pending resource update.");
	}
	const String manifest_path = p_directory.path_join("main_game.json");
	const String manifest_revision = WGodotFileStamp::get(manifest_path);
	Dictionary manifest;
	RETURN_IF_ERROR(read_manifest(manifest_path, manifest));
	WGodotResourceSnapshot snapshot;
	if (snapshot.load(p_directory.path_join(WGodotResourceSnapshot::FILENAME)) != OK || !manifest.has("resource_generation")) {
		r_error = "Run normal generation and packaging once to save the compiled resource schemas.";
		return ERR_UNCONFIGURED;
	}
	RETURN_IF_ERROR(unchanged(snapshot.settings, r_error));
	RETURN_IF_ERROR(unchanged(manifest["sources"], r_error));
	RETURN_IF_ERROR(unchanged(manifest["editor_only_scripts"], r_error));
	const Dictionary old_sources = manifest["resource_sources"];
	Dictionary revisions = old_sources.duplicate();
	const Dictionary compiled = manifest["compiled_resources"];
	const Dictionary paths = manifest["resource_paths"];
	PackedStringArray changed;
	for (const auto &entry : old_sources) {
		const String path = entry.key;
		const String revision = WGodotFileStamp::get(path);
		if (revision == String(entry.value)) {
			continue;
		}
		if (path.get_extension() != "tres" || revision == "<missing>" || !compiled.has(path)) {
			r_error = "This change requires normal generation/packaging: " + path;
			return ERR_UNAVAILABLE;
		}
		revisions[path] = revision;
		changed.push_back(path);
	}
	const String pack_revision = WGodotFileStamp::get(p_pack);
	WGodotResourcePack pack;
	RETURN_IF_ERROR(pack.open(p_pack));
	if (pack.read(WGodotResourceSnapshot::PACK_IDENTITY) != WGodotResourceSnapshot::pack_identity(manifest)) {
		r_error = "Pack and native module belong to different generations. Run normal packaging first.";
		return ERR_INVALID_DATA;
	}
	const uint64_t verified = OS::get_singleton()->get_ticks_msec();
	if (changed.is_empty()) {
		print_line(recovered ? vformat("Recovery complete: %d ms. No further resource changes.", verified - start) : vformat("Resources unchanged: %d ms. No payload, cache, or pack writes.", verified - start));
		return OK;
	}
	RETURN_IF_ERROR(DirAccess::make_dir_recursive_absolute(staging));
	HashMap<String, String> replacements;
	PackedStringArray filenames;
	changed.sort();
	for (const String &path : changed) {
		WGodotResourceDocument document(snapshot, paths, compiled);
		RETURN_IF_ERROR(document.load(path, r_error));
		const int64_t id = paths[path];
		const String filename = "resource_" + itos(id) + ".wgp";
		const String output = staging.path_join(filename);
		RETURN_IF_ERROR(document.encode(output, r_error));
		RETURN_IF_ERROR(document.cache());
		replacements.insert(WGodotNativePayload::path(id), output);
		filenames.push_back(filename);
	}
	const uint64_t encoded = OS::get_singleton()->get_ticks_msec();
	manifest["resource_sources"] = revisions;
	manifest["resource_generation"] = JSON::stringify(revisions, "", true).sha256_text();
	RETURN_IF_ERROR(write_manifest(staging.path_join("main_game.json"), manifest));
	RETURN_IF_ERROR(pack.rewrite(p_pack + ".resource-update", replacements, WGodotResourceSnapshot::PACK_IDENTITY, WGodotResourceSnapshot::pack_identity(manifest)));
	pack.close();
	// Do not publish bytes assembled from mixed revisions if files were edited
	// while encoding. The old playable pack remains intact on this path.
	RETURN_IF_ERROR(unchanged(revisions, r_error));
	RETURN_IF_ERROR(unchanged(snapshot.settings, r_error));
	RETURN_IF_ERROR(unchanged(manifest["sources"], r_error));
	if (WGodotFileStamp::get(manifest_path) != manifest_revision || WGodotFileStamp::get(p_pack) != pack_revision) {
		r_error = "Another build changed the native module or pack during this update.";
		return ERR_BUSY;
	}
	Dictionary journal;
	journal["pack"] = p_pack;
	journal["pack_revision"] = pack_revision;
	journal["files"] = filenames;
	RETURN_IF_ERROR(write_manifest(staging.path_join("transaction.tmp"), journal));
	RETURN_IF_ERROR(DirAccess::rename_absolute(staging.path_join("transaction.tmp"), staging.path_join("transaction.json")));
	RETURN_IF_ERROR(publish(p_directory, p_pack, r_error));
	print_line(vformat("Updated %d resource(s): revisions %d ms, protobuf + .res %d ms, pack + publication %d ms, total %d ms. No script analysis or C++ compilation.",
			changed.size(), verified - start, encoded - verified, OS::get_singleton()->get_ticks_msec() - encoded, OS::get_singleton()->get_ticks_msec() - start));
	return OK;
}
} // namespace

int WGodotResourceUpdate::run(const Vector<String> &p_arguments) {
	if (p_arguments.size() != 3 || p_arguments[1] != "--pack" || !p_arguments[0].is_absolute_path() || !p_arguments[2].is_absolute_path() || !ProjectSettings::get_singleton()->is_project_loaded()) {
		print_error("Usage: --path <project> --wg update-resources <absolute native module directory> --pack <absolute pack.pck>");
		return 2;
	}
	String message;
	const Error error = update(p_arguments[0], p_arguments[2], message);
	if (error != OK) {
		print_error("Resource update failed: " + String(error_names[error]) + ". " + message);
		return 1;
	}
	return 0;
}
