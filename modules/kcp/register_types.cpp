// wgodot-changes::file
#include "register_types.h"

#include "packet_peer_kcp.h"

#include "core/object/class_db.h"

void initialize_kcp_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(PacketPeerKCP);
	}
}

void uninitialize_kcp_module(ModuleInitializationLevel p_level) {
}
