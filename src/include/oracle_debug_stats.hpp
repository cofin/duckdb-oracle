#pragma once

#include "duckdb.hpp"

namespace duckdb {

struct OracleDebugStatsSnapshot {
	idx_t fetch_calls = 0;
	idx_t max_rows_per_fetch = 0;
	idx_t scan_buffer_bytes = 0;
	idx_t write_execute_calls = 0;
	idx_t max_write_iters = 0;
	idx_t write_buffer_bytes = 0;
	string last_query;
};

void OracleDebugResetStats();
void OracleDebugRecordFetch(idx_t rows);
void OracleDebugRecordScanBufferBytes(idx_t bytes);
void OracleDebugRecordWriteExecute(idx_t rows);
void OracleDebugRecordWriteBufferBytes(idx_t bytes);
void OracleDebugRecordQuery(const string &query);
OracleDebugStatsSnapshot OracleDebugGetStats();
idx_t OracleDebugGetCounter(const string &name);
string OracleDebugGetLastQuery();

} // namespace duckdb
