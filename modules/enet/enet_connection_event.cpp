// wgodot-changes::file
#include "enet_connection_event.h"
#include "core/object/class_db.h"

void ENetConnectionEvent::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_peer"), &ENetConnectionEvent::get_peer);
	ClassDB::bind_method(D_METHOD("get_packet"), &ENetConnectionEvent::get_packet);
	ClassDB::bind_method(D_METHOD("get_channel"), &ENetConnectionEvent::get_channel);
	ClassDB::bind_method(D_METHOD("get_data"), &ENetConnectionEvent::get_data);
	ClassDB::bind_method(D_METHOD("get_flags"), &ENetConnectionEvent::get_flags);
}
