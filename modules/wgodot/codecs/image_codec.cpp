// wgodot-changes::file
#include "image_codec.h"

#include "resource_codec.h"

#include "core/io/image.h"

namespace {
Error read_image(Image *p_image, VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error) {
	Variant parsed;
	RETURN_IF_ERROR(VariantParser::parse_value(p_token, parsed, p_stream, r_line, r_error));
	if (parsed.get_type() != Variant::DICTIONARY) {
		r_error = "Image.data requires a storage record.";
		return ERR_PARSE_ERROR;
	}
	const Dictionary record = parsed;
	const char *names[] = { "width", "height", "format", "mipmaps", "data" };
	const Variant::Type types[] = { Variant::INT, Variant::INT, Variant::STRING, Variant::BOOL, Variant::PACKED_BYTE_ARRAY };
	if (record.size() != 5) {
		r_error = "Image.data requires width, height, format, mipmaps and data.";
		return ERR_PARSE_ERROR;
	}
	for (int i = 0; i < 5; i++) {
		if (!record.has(names[i]) || record[names[i]].get_type() != types[i]) {
			r_error = "Image.data has a missing or incorrectly typed field: " + String(names[i]);
			return ERR_PARSE_ERROR;
		}
	}
	const int64_t width = record["width"];
	const int64_t height = record["height"];
	if (width <= 0 || width > Image::MAX_WIDTH || height <= 0 || height > Image::MAX_HEIGHT) {
		r_error = "Image dimensions are outside the native limits.";
		return ERR_PARSE_ERROR;
	}
	const String name = record["format"];
	Image::Format format = Image::FORMAT_MAX;
	for (int i = 0; i < Image::FORMAT_MAX; i++) {
		if (name == Image::get_format_name(Image::Format(i))) {
			format = Image::Format(i);
			break;
		}
	}
	if (format == Image::FORMAT_MAX) {
		r_error = "Unknown native image format: " + name;
		return ERR_PARSE_ERROR;
	}
	const bool mipmaps = record["mipmaps"];
	const PackedByteArray bytes = record["data"];
	if (bytes.size() != Image::get_image_data_size(width, height, format, mipmaps)) {
		r_error = "Image byte count does not match its dimensions, format and mipmaps.";
		return ERR_PARSE_ERROR;
	}
	if (p_image) {
		p_image->initialize_data(width, height, mipmaps, format, bytes);
	}
	return OK;
}

class ImageCodec : public WResourceCodec {
public:
	bool owns_property(const StringName &) const override { return false; }
	bool owns_native_property(const StringName &p_name) const override { return p_name == SNAME("data"); }
	bool requires_native_factory() const override { return true; }
	bool owns_assignment(const String &p_name) const override { return p_name == "data"; }
	Error read(Resource *p_resource, VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *) const override {
		return read_image(Object::cast_to<Image>(p_resource), p_token, p_stream, r_line, r_error);
	}
	Error check_types(VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *, const TypeContext &) const override {
		return read_image(nullptr, p_token, p_stream, r_line, r_error);
	}
	Error write(const Resource *, String &r_text, VariantWriter::EncodeResourceFunc, void *) const override {
		r_text = String();
		return OK;
	}
	Error write_native(const Resource *p_resource, const String &p_target, NativeWriter &p_writer) const override {
		const Image *image = Object::cast_to<Image>(p_resource);
		ERR_FAIL_NULL_V(image, ERR_INVALID_DATA);
		p_writer.include("core/io/image.h");
		const String bytes = p_writer.value(image->get_data());
		p_writer.line(p_target + "->initialize_data(" + itos(image->get_width()) + ", " + itos(image->get_height()) + ", " +
				p_writer.value(image->has_mipmaps()) + ", Image::Format(" + itos(image->get_format()) + "), " + bytes + ");");
		return OK;
	}
};

ImageCodec image_codec;
} // namespace

void register_image_codec() {
	WResourceCodec::register_codec("Image", &image_codec);
}

void unregister_image_codec() {
	WResourceCodec::unregister_codec("Image");
}
