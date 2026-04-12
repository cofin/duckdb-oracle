#include "oracle_insert.hpp"
#include "oracle_connection_manager.hpp"
#include "oracle_utils.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/numeric_utils.hpp"
#include "duckdb/parser/keyword_helper.hpp"

namespace duckdb {

//--- State classes ---

struct OracleInsertGlobalState : public GlobalSinkState {
	OracleInsertGlobalState(unique_ptr<OracleWriteBindData> bind_data_p, unique_ptr<GlobalFunctionData> gstate_p)
	    : bind_data(std::move(bind_data_p)), gstate(std::move(gstate_p)), insert_count(0) {
	}

	unique_ptr<OracleWriteBindData> bind_data;
	unique_ptr<GlobalFunctionData> gstate;
	idx_t insert_count;
};

struct OracleInsertLocalState : public LocalSinkState {
	OracleInsertLocalState() : lstate(make_uniq<OracleWriteLocalState>(nullptr, nullptr)) {
	}

	unique_ptr<LocalFunctionData> lstate;
};

//--- PhysicalOracleInsert ---

PhysicalOracleInsert::PhysicalOracleInsert(PhysicalPlan &physical_plan, vector<LogicalType> types, string table_name_p,
                                           string connection_string_p, vector<string> column_names_p,
                                           vector<LogicalType> column_types_p, idx_t estimated_cardinality)
    : PhysicalOperator(physical_plan, PhysicalOperatorType::EXTENSION, std::move(types), estimated_cardinality),
      table_name(std::move(table_name_p)), connection_string(std::move(connection_string_p)),
      column_names(std::move(column_names_p)), column_types(std::move(column_types_p)) {
}

unique_ptr<GlobalSinkState> PhysicalOracleInsert::GetGlobalSinkState(ClientContext &context) const {
	// Build OracleWriteBindData directly (bypass CopyFunctionBindInput)
	auto bind_data = make_uniq<OracleWriteBindData>();
	bind_data->table_name = table_name;
	bind_data->connection_string = connection_string;
	bind_data->column_names = column_names;
	bind_data->column_types = column_types;

	// Parse schema.table
	auto parts = StringUtil::Split(table_name, ".");
	if (parts.size() == 2) {
		bind_data->schema_name = parts[0];
		bind_data->object_name = parts[1];
	} else {
		bind_data->object_name = table_name;
	}

	// Set bind_types based on DuckDB LogicalType
	bind_data->oracle_types.resize(column_types.size(), "VARCHAR2");
	bind_data->bind_types.resize(column_types.size(), SQLT_CHR);

	for (idx_t i = 0; i < column_types.size(); i++) {
		switch (column_types[i].id()) {
		case LogicalTypeId::TINYINT:
		case LogicalTypeId::SMALLINT:
		case LogicalTypeId::INTEGER:
		case LogicalTypeId::BIGINT:
			bind_data->bind_types[i] = SQLT_INT;
			break;
		case LogicalTypeId::FLOAT:
		case LogicalTypeId::DOUBLE:
			bind_data->bind_types[i] = SQLT_BDOUBLE;
			break;
		case LogicalTypeId::DATE:
			bind_data->bind_types[i] = SQLT_ODT;
			break;
		case LogicalTypeId::TIMESTAMP:
		case LogicalTypeId::TIMESTAMP_TZ:
		case LogicalTypeId::TIMESTAMP_SEC:
		case LogicalTypeId::TIMESTAMP_MS:
		case LogicalTypeId::TIMESTAMP_NS:
			bind_data->bind_types[i] = SQLT_CHR;
			break;
		case LogicalTypeId::BLOB:
			bind_data->bind_types[i] = SQLT_BIN;
			break;
		default:
			bind_data->bind_types[i] = SQLT_CHR;
			break;
		}
	}

	// Introspect Oracle table to get actual column types
	if (!connection_string.empty()) {
		try {
			OracleConnection temp_conn;
			temp_conn.Connect(connection_string);

			string schema_filter = bind_data->schema_name.empty() ? "owner = SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA')"
			                                                      : "owner = upper('" + bind_data->schema_name + "')";
			string table_filter = "(table_name = '" + bind_data->object_name + "' OR table_name = upper('" +
			                      bind_data->object_name + "'))";

			string query = "SELECT owner, table_name, column_name, data_type FROM all_tab_columns WHERE " +
			               schema_filter + " AND " + table_filter + " ORDER BY owner, table_name, column_id";

			auto query_res = temp_conn.Query(query);

			string best_table_name;
			string best_owner;
			bool found_exact = false;
			std::unordered_map<string, string> col_type_map;
			std::unordered_map<string, string> col_name_map;

			for (auto &row : query_res.rows) {
				if (row.size() < 4) {
					continue;
				}
				const string &owner = row[0];
				const string &table = row[1];
				const string &col = row[2];
				const string &type = row[3];

				if (best_table_name.empty()) {
					best_table_name = table;
					best_owner = owner;
				}

				if (table != best_table_name) {
					if (table == bind_data->object_name && !found_exact) {
						best_table_name = table;
						best_owner = owner;
						col_type_map.clear();
						col_name_map.clear();
						found_exact = true;
					} else if (found_exact) {
						continue;
					}
				}

				if (table == bind_data->object_name) {
					found_exact = true;
				}

				if (table == best_table_name) {
					col_type_map[col] = type;
					col_name_map[StringUtil::Upper(col)] = col;
				}
			}

			if (!best_table_name.empty()) {
				bind_data->object_name = best_table_name;
				if (bind_data->schema_name.empty()) {
					bind_data->schema_name = best_owner;
				}
			}

			for (idx_t i = 0; i < column_names.size(); i++) {
				string col_upper = StringUtil::Upper(column_names[i]);
				string actual_name;
				if (col_name_map.count(col_upper)) {
					actual_name = col_name_map[col_upper];
				} else if (col_type_map.count(column_names[i])) {
					actual_name = column_names[i];
				}

				if (!actual_name.empty()) {
					bind_data->column_names[i] = actual_name;
					if (col_type_map.count(actual_name)) {
						bind_data->oracle_types[i] = col_type_map[actual_name];
					}
				}
			}
		} catch (std::exception &e) {
			if (getenv("ORACLE_DEBUG")) {
				fprintf(stderr, "Warning: Failed to fetch metadata in Insert: %s\n", e.what());
			}
		}
	}

	// Initialize global write state (reuses OracleWriteInitGlobal logic)
	auto gstate = OracleWriteInitGlobal(context, *bind_data, bind_data->table_name);

	return make_uniq<OracleInsertGlobalState>(std::move(bind_data), std::move(gstate));
}

unique_ptr<LocalSinkState> PhysicalOracleInsert::GetLocalSinkState(ExecutionContext &context) const {
	return make_uniq<OracleInsertLocalState>();
}

SinkResultType PhysicalOracleInsert::Sink(ExecutionContext &context, DataChunk &chunk, OperatorSinkInput &input) const {
	auto &gstate = input.global_state.Cast<OracleInsertGlobalState>();
	auto &lstate = input.local_state.Cast<OracleInsertLocalState>();

	// Delegate to existing write sink
	OracleWriteSink(context, *gstate.bind_data, *gstate.gstate, *lstate.lstate, chunk);
	gstate.insert_count += chunk.size();

	return SinkResultType::NEED_MORE_INPUT;
}

SinkCombineResultType PhysicalOracleInsert::Combine(ExecutionContext &context, OperatorSinkCombineInput &input) const {
	return SinkCombineResultType::FINISHED;
}

SinkFinalizeType PhysicalOracleInsert::Finalize(Pipeline &pipeline, Event &event, ClientContext &context,
                                                OperatorSinkFinalizeInput &input) const {
	auto &gstate = input.global_state.Cast<OracleInsertGlobalState>();

	// Commit the transaction
	OracleWriteFinalize(context, *gstate.bind_data, *gstate.gstate);

	return SinkFinalizeType::READY;
}

SourceResultType PhysicalOracleInsert::GetDataInternal(ExecutionContext &context, DataChunk &chunk,
                                                       OperatorSourceInput &input) const {
	auto &gstate = sink_state->Cast<OracleInsertGlobalState>();
	chunk.SetCardinality(1);
	chunk.SetValue(0, 0, Value::BIGINT(NumericCast<int64_t>(gstate.insert_count)));
	return SourceResultType::FINISHED;
}

} // namespace duckdb
