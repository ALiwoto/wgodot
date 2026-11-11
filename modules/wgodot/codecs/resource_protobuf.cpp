// wgodot-changes::file
#include "resource_protobuf.h"

#include "core/io/wgodot_resource_paths.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

namespace WGodotResourceProtobuf {
namespace {
const Codec *const *codecs = nullptr;
int codec_count = 0;
HashMap<uint64_t, const Codec *> codec_ids;

const Codec *find(uint64_t p_id) {
	const Codec *const *codec = codec_ids.getptr(p_id);
	return codec ? *codec : nullptr;
}

struct Record {
	const Codec *codec = nullptr;
	Ref<Resource> resource;
	Reader fields;
	Reader shared;
	uint8_t state = 0;
};

class LoadContext : public Context {
public:
	Vector<Record> records;
	ResourceFormatLoader::CacheMode cache_mode;
	bool valid = true;

	explicit LoadContext(ResourceFormatLoader::CacheMode p_mode) : cache_mode(p_mode) {}
	Reference identify(const Ref<Resource> &) override { return {}; }
	Ref<Resource> resolve(const Reference &p_reference) override {
		if (p_reference.external) {
			return ResourceLoader::load(WGodotResourcePaths::to_path(p_reference.external), String(), cache_mode);
		}
		if (!p_reference.local) {
			return Ref<Resource>();
		}
		if (p_reference.local > uint64_t(records.size()) || !populate(p_reference.local - 1)) {
			valid = false;
			return Ref<Resource>();
		}
		return records[p_reference.local - 1].resource;
	}
	bool populate(uint32_t p_index) {
		Record &record = records.write[p_index];
		if (record.state) {
			return true;
		}
		record.state = 1;
		HashSet<uint32_t> assigned;
		Reader fields = record.fields;
		while (fields.valid && fields.remaining()) {
			const uint64_t tag = fields.varint();
			if (!fields.valid || (tag >> 3) > 536870911) {
				return false;
			}
			const uint32_t id = tag >> 3;
			const Field *field = record.codec->find(id);
			if (!field || assigned.has(id) || !field->read(record.resource.ptr(), fields, tag & 7, *this)) {
				return false;
			}
			assigned.insert(id);
		}
		Reader shared = record.shared;
		while (shared.valid && shared.remaining()) {
			if (shared.varint() != 10) {
				return false;
			}
			Reader link = shared.slice(shared.varint());
			if (link.varint() != 8) {
				return false;
			}
			const uint64_t destination = link.varint();
			if (link.varint() != 16) {
				return false;
			}
			const int64_t external = link.varint();
			if (link.varint() != 24) {
				return false;
			}
			const uint64_t member = link.varint();
			if (!link.valid || link.remaining() || !external || destination > 536870911 || member > 536870911 || assigned.has(destination)) {
				return false;
			}
			const Ref<Resource> source = resolve({ 0, external });
			const Codec *source_codec = nullptr;
			for (int i = 0; source.is_valid() && i < codec_count; i++) {
				if (source->get_class() == StringName(codecs[i]->name)) {
					source_codec = codecs[i];
					break;
				}
			}
			const Field *from = source_codec ? source_codec->find(member) : nullptr;
			const Field *to = record.codec->find(destination);
			if (!from || !to) {
				return false;
			}
			// The typed source writer and destination reader share one schema.
			// Container sharing uses the binding's direct assignment hook below.
			if (!to->share || !to->share(record.resource.ptr(), source.ptr(), *from)) {
				return false;
			}
			assigned.insert(destination);
		}
		record.state = 2;
		return valid && fields.valid && shared.valid;
	}
};
} // namespace

void Value<Reference>::write(Writer &r_writer, const Reference &p_value, Context &) {
	Writer message_value;
	if (p_value.local) {
		message_value.tag(1, 0);
		message_value.varint(p_value.local);
	} else if (p_value.external) {
		message_value.tag(2, 0);
		message_value.varint(p_value.external);
	}
	message(r_writer, message_value);
}

bool Value<Reference>::read(Reader &r_reader, Reference &r_value, Context &) {
	Reader contents = r_reader.slice(r_reader.varint());
	if (contents.remaining()) {
		const uint64_t tag = contents.varint();
		if (tag == 8) {
			r_value.local = contents.varint();
		} else if (tag == 16) {
			r_value.external = contents.varint();
		} else {
			return false;
		}
	}
	return contents.valid && !contents.remaining();
}

const Field *Codec::find(uint32_t p_id) const {
	return p_id && p_id <= field_count && fields[p_id - 1].id == p_id ? &fields[p_id - 1] : nullptr;
}

void initialize(const Codec *const *p_codecs, int p_count) {
	codecs = p_codecs;
	codec_count = p_count;
	codec_ids.clear();
	for (int i = 0; i < p_count; i++) {
		codec_ids.insert(p_codecs[i]->id, p_codecs[i]);
	}
}

Ref<Resource> load(int64_t p_id, ResourceFormatLoader::CacheMode p_cache_mode) {
	Error error = OK;
	const Vector<uint8_t> bytes = FileAccess::get_file_as_bytes(WGodotNativePayload::path(p_id), &error);
	ERR_FAIL_COND_V(error != OK, Ref<Resource>());
	Reader input{ bytes.ptr(), bytes.size() };
	ERR_FAIL_COND_V_MSG(input.fixed(4) != 0x42504757 || input.varint() != 1, Ref<Resource>(), "Invalid resource protobuf version.");
	LoadContext context(p_cache_mode == ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP || p_cache_mode == ResourceFormatLoader::CACHE_MODE_REPLACE_DEEP ? p_cache_mode : ResourceFormatLoader::CACHE_MODE_REUSE);
	while (input.valid && input.remaining()) {
		ERR_FAIL_COND_V(input.varint() != 10, Ref<Resource>());
		Reader object = input.slice(input.varint());
		ERR_FAIL_COND_V(object.varint() != 8, Ref<Resource>());
		const Codec *codec = find(object.varint());
		ERR_FAIL_COND_V(!codec || object.varint() != 17 || object.fixed(8) != codec->schema, Ref<Resource>());
		Record record;
		record.codec = codec;
		record.resource = codec->create();
		ERR_FAIL_COND_V(record.resource.is_null() || object.varint() != 26, Ref<Resource>());
		record.fields = object.slice(object.varint());
		ERR_FAIL_COND_V(object.varint() != 34, Ref<Resource>());
		record.shared = object.slice(object.varint());
		ERR_FAIL_COND_V(!object.valid || object.remaining(), Ref<Resource>());
		context.records.push_back(record);
	}
	ERR_FAIL_COND_V(!input.valid || context.records.is_empty() || !context.populate(0), Ref<Resource>());
	return context.records[0].resource;
}
} // namespace WGodotResourceProtobuf
