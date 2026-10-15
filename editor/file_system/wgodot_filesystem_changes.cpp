// wgodot-changes::file
#include "editor_file_system.h"

#include "core/io/wgodot_resource_trace.h"

bool EditorFileSystem::_prepare_changed_directories(bool p_use_notifications) {
	WGodotFileSystemWatcher::Changes changes = file_watcher.take_changes();
	filtered_change_scan = p_use_notifications && changes.available && !changes.rescan_all;
	changed_directories.clear();
	changed_directory_ancestors.clear();
	changed_files.clear();
	for (const String &path : changes.paths) {
		// Preserve precise notifications even when the caller requests a full scan.
		changed_files.insert(path.trim_suffix(".import"));
	}
	if (!filtered_change_scan) {
		return true;
	}
	HashSet<String> parents;
	for (const String &path : changes.paths) {
		parents.insert(path.get_base_dir());
	}
	for (String directory : parents) {
		// Created/deleted subtrees are reconciled by their nearest existing parent.
		while (directory != "res://" && (!get_filesystem_path(directory) || !DirAccess::dir_exists_absolute(directory))) {
			directory = directory.get_base_dir();
		}
		changed_directories.insert(directory);
		while (true) {
			changed_directory_ancestors.insert(directory);
			if (directory == "res://") {
				break;
			}
			directory = directory.get_base_dir();
		}
	}
	WGodotResourceTrace trace("filesystem.changed_directories", vformat("%d directories, %d paths", changed_directories.size(), changed_files.size()));
	return !changed_directories.is_empty();
}
