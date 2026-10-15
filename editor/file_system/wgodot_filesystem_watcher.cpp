// wgodot-changes::file
#include "wgodot_filesystem_watcher.h"

#include "core/os/memory.h"

#if defined(WINDOWS_ENABLED) && defined(THREADS_ENABLED)
#include "core/os/mutex.h"
#include "core/os/thread.h"

#include "platform/windows/windows.h"

struct WGodotFileSystemWatcher::State {
	HANDLE directory = INVALID_HANDLE_VALUE;
	HANDLE stop_event = nullptr;
	OVERLAPPED overlapped = {};
	DWORD buffer[16384]; // DWORD-aligned, 64 KiB (also supported by network shares).
	Thread thread;
	Mutex mutex;
	Changes pending;

	bool arm() {
		ResetEvent(overlapped.hEvent);
		return ReadDirectoryChangesW(directory, buffer, DWORD(sizeof(buffer)), TRUE,
				FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_ATTRIBUTES,
				nullptr, &overlapped, nullptr);
	}

	void record(const String &p_relative, DWORD p_action) {
		// These are generated companion/temporary files, not source modifications.
		if (p_relative.ends_with(".uid") || p_relative.ends_with(".tmp")) {
			return;
		}
		// Cache writes are import output, not source changes. Cache removal still
		// needs reconciliation so missing imported files can be regenerated.
		if (p_relative == ".godot" || p_relative == ".godot/imported" || p_relative.begins_with(".godot/imported/")) {
			if (p_action == FILE_ACTION_REMOVED || p_action == FILE_ACTION_RENAMED_OLD_NAME) {
				pending.rescan_all = true;
			}
			return;
		}
		for (const String &component : p_relative.split("/")) {
			if (component.begins_with(".") && component != ".gdignore") {
				return;
			}
		}
		if (p_relative.get_file() == ".gdignore" || p_relative.get_file() == "project.godot") {
			pending.rescan_all = true;
		}
		if (!pending.rescan_all) {
			pending.paths.insert("res://" + p_relative);
			if (pending.paths.size() >= 8192) {
				pending.rescan_all = true;
			}
		}
		if (pending.rescan_all) {
			pending.paths.clear();
		}
	}

	static void run(void *p_data) {
		State *self = static_cast<State *>(p_data);
		Thread::set_name("Project file watcher");
		const HANDLE events[] = { self->stop_event, self->overlapped.hEvent };
		while (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
			DWORD bytes = 0;
			const bool completed = GetOverlappedResult(self->directory, &self->overlapped, &bytes, FALSE);
			const DWORD error = completed ? ERROR_SUCCESS : GetLastError();
			{
				MutexLock lock(self->mutex);
				if (!completed || bytes == 0) {
					self->pending.rescan_all = true;
					self->pending.paths.clear();
				} else {
					DWORD offset = 0;
					while (offset < bytes) {
						const FILE_NOTIFY_INFORMATION *entry = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(reinterpret_cast<const uint8_t *>(self->buffer) + offset);
						const String relative = String::utf16(reinterpret_cast<const char16_t *>(entry->FileName), int(entry->FileNameLength / sizeof(WCHAR))).replace("\\", "/");
						self->record(relative, entry->Action);
						if (entry->NextEntryOffset == 0) {
							break;
						}
						offset += entry->NextEntryOffset;
					}
				}
			}
			if ((!completed && error != ERROR_NOTIFY_ENUM_DIR) || !self->arm()) {
				break;
			}
		}
		CancelIoEx(self->directory, &self->overlapped);
		DWORD ignored = 0;
		GetOverlappedResult(self->directory, &self->overlapped, &ignored, TRUE);
		MutexLock lock(self->mutex);
		self->pending.available = false;
	}

	~State() {
		if (thread.is_started()) {
			SetEvent(stop_event);
			thread.wait_to_finish();
		}
		if (directory != INVALID_HANDLE_VALUE) {
			CloseHandle(directory);
		}
		if (overlapped.hEvent) {
			CloseHandle(overlapped.hEvent);
		}
		if (stop_event) {
			CloseHandle(stop_event);
		}
	}
};

void WGodotFileSystemWatcher::start(const String &p_root) {
	ERR_FAIL_COND(state);
	state = memnew(State);
	state->directory = CreateFileW(reinterpret_cast<LPCWSTR>(p_root.utf16().get_data()), FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
	state->stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	state->overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (state->directory == INVALID_HANDLE_VALUE || !state->stop_event || !state->overlapped.hEvent || !state->arm()) {
		WARN_PRINT("Filesystem notifications unavailable; project focus will use full scans.");
		memdelete(state);
		state = nullptr;
		return;
	}
	state->pending.available = true;
	state->thread.start(State::run, state);
}

WGodotFileSystemWatcher::Changes WGodotFileSystemWatcher::take_changes() {
	if (!state) {
		return Changes();
	}
	MutexLock lock(state->mutex);
	Changes changes = state->pending;
	state->pending.paths.clear();
	state->pending.rescan_all = false;
	return changes;
}

WGodotFileSystemWatcher::~WGodotFileSystemWatcher() {
	if (state) {
		memdelete(state);
	}
}
#else
void WGodotFileSystemWatcher::start(const String &p_root) {}

WGodotFileSystemWatcher::Changes WGodotFileSystemWatcher::take_changes() {
	return Changes();
}

WGodotFileSystemWatcher::~WGodotFileSystemWatcher() {}
#endif
