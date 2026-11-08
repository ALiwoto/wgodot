// wgodot-changes::file
#include "curve_codec.h"

#include "curve_payload.h"
#include "resource_codec.h"

#include "scene/resources/curve.h"

namespace {
class CurveCodec : public WResourceCodec {
public:
	bool owns_property(const StringName &) const override { return false; }
	bool owns_native_property(const StringName &p_name) const override {
		return p_name == SNAME("_limits") || p_name == SNAME("_data") || p_name == SNAME("point_count");
	}
	bool requires_native_factory() const override { return true; }
	bool owns_assignment(const String &) const override { return false; }
	Error read(Resource *, VariantParser::Token &, VariantParser::Stream *, int &, String &, VariantParser::ResourceParser *) const override {
		return ERR_UNAVAILABLE;
	}
	Error write(const Resource *, String &r_text, VariantWriter::EncodeResourceFunc, void *) const override {
		r_text = String();
		return OK;
	}
	Error write_native(const Resource *p_resource, const String &p_target, NativeWriter &p_writer) const override {
		const Curve *curve = Object::cast_to<Curve>(p_resource);
		ERR_FAIL_NULL_V(curve, ERR_INVALID_DATA);
		p_writer.include("modules/wgodot/codecs/curve_payload.h");
		const String minimum = p_writer.value(curve->get_min_value());
		const String maximum = p_writer.value(curve->get_max_value());
		const String domain_minimum = p_writer.value(curve->get_min_domain());
		const String domain_maximum = p_writer.value(curve->get_max_domain());
		p_writer.line(p_target + "->set_native_limits(" + minimum + ", " + maximum + ", " + domain_minimum + ", " + domain_maximum + ");");
		const String points = p_writer.payload("LocalVector<Curve::Point>", WGodotNativePayload::CURVE_POINTS, [&](FileAccess &file) {
			file.store_64(curve->get_point_count());
			for (int i = 0; i < curve->get_point_count(); i++) {
				WGodotNativePayload::Value<Curve::Point>::write(file, curve->get_point(i));
			}
		});
		p_writer.line(p_target + "->set_native_points(" + points + ");");
		return OK;
	}
};

CurveCodec curve_codec;
} // namespace

void register_curve_codec() {
	WResourceCodec::register_codec("Curve", &curve_codec);
}

void unregister_curve_codec() {
	WResourceCodec::unregister_codec("Curve");
}
