// wgodot-changes::file
#include "register_types.h"

#include "postgresql_connection.h"

void initialize_postgresql_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(PostgreSQLConnectionOptions);
		GDREGISTER_CLASS(PostgreSQLParameters);
		GDREGISTER_CLASS(PostgreSQLResult);
		GDREGISTER_CLASS(PostgreSQLConnection);
	}
}

void uninitialize_postgresql_module(ModuleInitializationLevel p_level) {
}
