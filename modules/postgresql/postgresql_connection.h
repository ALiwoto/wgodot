// wgodot-changes::file
#pragma once

#include "postgresql_connection_options.h"
#include "postgresql_parameters.h"
#include "postgresql_result.h"

struct pg_conn;

// One owner thread and one outstanding command per connection. No implicit retry.
class PostgreSQLConnection : public RefCounted {
	GDCLASS(PostgreSQLConnection, RefCounted);

public:
	enum State {
		STATE_DISCONNECTED,
		STATE_CONNECTING,
		STATE_READY,
		STATE_BUSY,
		STATE_RESULT_READY,
		STATE_FAILED,
	};
	enum TransactionState {
		TRANSACTION_UNKNOWN,
		TRANSACTION_IDLE,
		TRANSACTION_ACTIVE,
		TRANSACTION_OPEN,
		TRANSACTION_FAILED,
	};

private:
	pg_conn *connection = nullptr;
	State state = STATE_DISCONNECTED;
	int connect_poll_state = 0;
	uint64_t deadline_usec = 0;
	String last_error;
	Ref<PostgreSQLResult> pending_result;
	Error fail(Error p_error, const String &p_message);

protected:
	static void _bind_methods();

public:
	// Requires a numeric address to avoid synchronous DNS. TLS verifies the supplied
	// hostname (or the address); TLS_DISABLED is an explicit connection option.
	Error start_connection(const Ref<PostgreSQLConnectionOptions> &p_options, int p_timeout_ms = 10000);
	// One SQL statement, binary parameters/results. Bound large reads in SQL.
	// BEGIN/COMMIT/ROLLBACK use the same connection; pooling belongs above this API.
	Error send_query(const String &p_sql, const Ref<PostgreSQLParameters> &p_parameters = Ref<PostgreSQLParameters>(), int p_timeout_ms = 10000);
	// Call until READY. On RESULT_READY, take_result() then continue polling to drain.
	// Timeouts close the connection; a sent mutation's outcome may be unknown.
	Error poll();
	Ref<PostgreSQLResult> take_result();
	State get_state() const { return state; }
	TransactionState get_transaction_state() const;
	String get_last_error() const { return last_error; }
	void close();
	~PostgreSQLConnection();
};

VARIANT_ENUM_CAST(PostgreSQLConnection::State);
VARIANT_ENUM_CAST(PostgreSQLConnection::TransactionState);
