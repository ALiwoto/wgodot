// wgodot-changes::file
#pragma once

#include "core/io/resource.h"
#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/variant/typed_array.h"

#include <initializer_list>

class WGodotPreloads : public Object {
	GDCLASS(WGodotPreloads, Object);

public:
	struct Definition {
		const char *path;
		const char *type;
		bool asynchronous;
	};

private:
	enum State { PENDING, LOADING, READY, FAILED };
	struct Entry {
		String path;
		String type;
		bool asynchronous = false;
		bool requested = false;
		State state = PENDING;
		Error error = OK;
		Ref<Resource> resource;
	};
	static WGodotPreloads *singleton;
	static const Definition *definitions;
	static int definition_count;
	Vector<Entry> entries;
	mutable Mutex mutex;
	bool loading_synchronous = false;
	int total_count = 0;
	int loaded_count = 0;
	int failed_count = 0;

	Error load_synchronous(int p_index);
	void collect(int p_index);

protected:
	static void _bind_methods();

public:
	static WGodotPreloads *get_singleton() { return singleton; }
	static void initialize();
	static void deinitialize();
	static void set_manifest(const Definition *p_definitions, int p_count);
	Error start();
	void poll();
	void finish();
	Ref<Resource> get_resource(int p_index);
	bool require_resources(std::initializer_list<int> p_indices);

	bool is_finished() const;
	bool has_failed() const;
	int get_total_count() const;
	int get_loaded_count() const;
	int get_failed_count() const;
	int get_pending_count() const;
	double get_progress() const;
	TypedArray<Dictionary> get_failures() const;

	WGodotPreloads();
	~WGodotPreloads();
};
