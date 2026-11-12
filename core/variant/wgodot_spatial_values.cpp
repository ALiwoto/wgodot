// wgodot-changes::file
#include "variant.h"
#include "variant_pools.h"

Variant::Variant(const WVector3d &p_vector) : type(WVECTOR3D) {
	_data._wvector3d = VariantPools::alloc<WVector3d>();
	memnew_placement(_data._wvector3d, WVector3d(p_vector));
}

Variant::Variant(const WTransform3d &p_transform) : type(WTRANSFORM3D) {
	_data._wtransform3d = VariantPools::alloc<WTransform3d>();
	memnew_placement(_data._wtransform3d, WTransform3d(p_transform));
}

Variant::operator WVector3d() const {
	return type == WVECTOR3D ? *_data._wvector3d : WVector3d();
}

Variant::operator WTransform3d() const {
	return type == WTRANSFORM3D ? *_data._wtransform3d : WTransform3d();
}
