#include "oracle_pushdown.hpp"
#include "oracle_table_function.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_operator_expression.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"
#include <cstdio>

namespace duckdb {

static string ColumnRefSQL(const string &col_name) {
	return KeywordHelper::WriteQuoted(col_name, '"');
}

static string OracleQuoteStringLiteral(const string &value) {
	return "'" + StringUtil::Replace(value, "'", "''") + "'";
}

static bool ConstantToSQL(Expression &expr, string &out_sql) {
	if (expr.type != ExpressionType::VALUE_CONSTANT) {
		return false;
	}
	auto &c = expr.Cast<BoundConstantExpression>();
	if (c.value.IsNull()) {
		out_sql = "NULL";
		return true;
	}

	switch (c.value.type().id()) {
	case LogicalTypeId::VARCHAR:
		out_sql = OracleQuoteStringLiteral(c.value.GetValue<string>());
		return true;
	case LogicalTypeId::DATE: {
		auto date = c.value.GetValue<date_t>();
		int32_t year, month, day;
		Date::Convert(date, year, month, day);
		out_sql = "DATE " + OracleQuoteStringLiteral(Date::Format(year, month, day));
		return true;
	}
	case LogicalTypeId::TINYINT:
	case LogicalTypeId::SMALLINT:
	case LogicalTypeId::INTEGER:
	case LogicalTypeId::BIGINT:
	case LogicalTypeId::UTINYINT:
	case LogicalTypeId::USMALLINT:
	case LogicalTypeId::UINTEGER:
	case LogicalTypeId::UBIGINT:
	case LogicalTypeId::FLOAT:
	case LogicalTypeId::DOUBLE:
	case LogicalTypeId::DECIMAL:
		out_sql = c.value.ToString();
		return true;
	case LogicalTypeId::TIMESTAMP_TZ:
		// Oracle TIMESTAMP WITH TIME ZONE needs timezone-aware literal syntax.
		return false;
	case LogicalTypeId::TIMESTAMP:
	case LogicalTypeId::TIMESTAMP_SEC:
	case LogicalTypeId::TIMESTAMP_MS:
	case LogicalTypeId::TIMESTAMP_NS:
		out_sql = "TIMESTAMP " + OracleQuoteStringLiteral(c.value.ToString());
		return true;
	default:
		return false;
	}
}

static idx_t ResolveBoundRefColumnIndex(idx_t ref_idx, const vector<ColumnIndex> &column_ids) {
	if (ref_idx < column_ids.size()) {
		auto &column_id = column_ids[ref_idx];
		if (column_id.HasPrimaryIndex()) {
			return column_id.GetPrimaryIndex();
		}
	}
	return ref_idx;
}

static bool HasDuplicateColumnNames(const vector<string> &names) {
	for (idx_t i = 0; i < names.size(); i++) {
		for (idx_t j = i + 1; j < names.size(); j++) {
			if (names[i] == names[j]) {
				return true;
			}
		}
	}
	return false;
}

static bool TryExtractComparison(Expression &expr, const vector<string> &names, const vector<ColumnIndex> &column_ids,
                                 string &out_clause) {
	if (expr.type != ExpressionType::COMPARE_EQUAL && expr.type != ExpressionType::COMPARE_LESSTHAN &&
	    expr.type != ExpressionType::COMPARE_GREATERTHAN && expr.type != ExpressionType::COMPARE_LESSTHANOREQUALTO &&
	    expr.type != ExpressionType::COMPARE_GREATERTHANOREQUALTO) {
		if (getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] TryExtractComparison: not a comparison, type=%d\n", (int)expr.type);
		}
		return false;
	}
	auto &cmp = expr.Cast<BoundComparisonExpression>();
	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] TryExtractComparison: left_type=%d, right_type=%d\n", (int)cmp.left->type,
		        (int)cmp.right->type);
	}
	Expression *const_expr = nullptr;
	ExpressionType op_type = expr.type;

	auto is_column_ref = [](Expression *e) {
		return e->type == ExpressionType::BOUND_REF || e->type == ExpressionType::BOUND_COLUMN_REF;
	};

	auto get_column_index = [&column_ids](Expression *e) -> idx_t {
		if (e->type == ExpressionType::BOUND_REF) {
			return ResolveBoundRefColumnIndex(e->Cast<BoundReferenceExpression>().index, column_ids);
		} else if (e->type == ExpressionType::BOUND_COLUMN_REF) {
			return ResolveBoundRefColumnIndex(e->Cast<BoundColumnRefExpression>().binding.column_index, column_ids);
		}
		return DConstants::INVALID_INDEX;
	};

	idx_t col_idx = DConstants::INVALID_INDEX;
	if (is_column_ref(cmp.left.get()) && cmp.right->type == ExpressionType::VALUE_CONSTANT) {
		col_idx = get_column_index(cmp.left.get());
		const_expr = cmp.right.get();
	} else if (is_column_ref(cmp.right.get()) && cmp.left->type == ExpressionType::VALUE_CONSTANT) {
		col_idx = get_column_index(cmp.right.get());
		const_expr = cmp.left.get();
		switch (op_type) {
		case ExpressionType::COMPARE_GREATERTHAN:
			op_type = ExpressionType::COMPARE_LESSTHAN;
			break;
		case ExpressionType::COMPARE_GREATERTHANOREQUALTO:
			op_type = ExpressionType::COMPARE_LESSTHANOREQUALTO;
			break;
		case ExpressionType::COMPARE_LESSTHAN:
			op_type = ExpressionType::COMPARE_GREATERTHAN;
			break;
		case ExpressionType::COMPARE_LESSTHANOREQUALTO:
			op_type = ExpressionType::COMPARE_GREATERTHANOREQUALTO;
			break;
		default:
			break;
		}
	} else {
		if (getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] TryExtractComparison: no column_ref + VALUE_CONSTANT pattern\n");
		}
		return false;
	}

	if (col_idx == DConstants::INVALID_INDEX || col_idx >= names.size()) {
		if (getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] TryExtractComparison: col_idx=%lu >= names.size()=%lu\n", (unsigned long)col_idx,
			        (unsigned long)names.size());
		}
		return false;
	}
	string const_sql;
	if (!ConstantToSQL(*const_expr, const_sql)) {
		if (getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] TryExtractComparison: ConstantToSQL failed\n");
		}
		return false;
	}

	string op;
	switch (op_type) {
	case ExpressionType::COMPARE_EQUAL:
		op = "=";
		break;
	case ExpressionType::COMPARE_GREATERTHAN:
		op = ">";
		break;
	case ExpressionType::COMPARE_GREATERTHANOREQUALTO:
		op = ">=";
		break;
	case ExpressionType::COMPARE_LESSTHAN:
		op = "<";
		break;
	case ExpressionType::COMPARE_LESSTHANOREQUALTO:
		op = "<=";
		break;
	default:
		return false;
	}

	out_clause = ColumnRefSQL(names[col_idx]) + " " + op + " " + const_sql;
	return true;
}

static bool TryExtractIsNull(Expression &expr, const vector<string> &names, const vector<ColumnIndex> &column_ids,
                             string &out_clause) {
	if (expr.type != ExpressionType::OPERATOR_IS_NULL) {
		return false;
	}
	auto &op = expr.Cast<BoundOperatorExpression>();
	if (op.children.size() != 1) {
		return false;
	}

	idx_t col_idx = DConstants::INVALID_INDEX;
	auto child_type = op.children[0]->type;
	if (child_type == ExpressionType::BOUND_REF) {
		col_idx = ResolveBoundRefColumnIndex(op.children[0]->Cast<BoundReferenceExpression>().index, column_ids);
	} else if (child_type == ExpressionType::BOUND_COLUMN_REF) {
		col_idx = ResolveBoundRefColumnIndex(op.children[0]->Cast<BoundColumnRefExpression>().binding.column_index,
		                                     column_ids);
	} else {
		return false;
	}

	if (col_idx == DConstants::INVALID_INDEX || col_idx >= names.size()) {
		return false;
	}
	out_clause = ColumnRefSQL(names[col_idx]) + " IS NULL";
	return true;
}

void OraclePushdownComplexFilter(ClientContext &, LogicalGet &get, FunctionData *bind_data_p,
                                 vector<unique_ptr<Expression>> &expressions) {
	auto &bind = bind_data_p->Cast<OracleBindData>();
	if (!bind.settings.enable_pushdown) {
		return;
	}

	if (HasDuplicateColumnNames(bind.original_names) || HasDuplicateColumnNames(bind.column_names)) {
		if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] pushdown: duplicate column names; leaving filters/projection to DuckDB\n");
		}
		return;
	}

	if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] pushdown: expressions=%lu, column_names=%lu\n", (unsigned long)expressions.size(),
		        (unsigned long)bind.column_names.size());
		for (idx_t i = 0; i < bind.column_names.size(); i++) {
			fprintf(stderr, "[oracle] pushdown: column_names[%lu]=%s\n", (unsigned long)i,
			        bind.column_names[i].c_str());
		}
	}

	vector<unique_ptr<Expression>> remaining;
	vector<string> clauses;
	for (auto &expr : expressions) {
		string clause;
		if (TryExtractComparison(*expr, bind.column_names, get.GetColumnIds(), clause) ||
		    TryExtractIsNull(*expr, bind.column_names, get.GetColumnIds(), clause)) {
			if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
				fprintf(stderr, "[oracle] pushdown: extracted clause: %s\n", clause.c_str());
			}
			clauses.push_back(std::move(clause));
			continue;
		}
		if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] pushdown: could not extract expression type=%d\n", (int)expr->type);
		}
		remaining.push_back(std::move(expr));
	}

	string where_sql;
	if (!clauses.empty()) {
		where_sql = " WHERE " + StringUtil::Join(clauses, " AND ");
	}

	vector<string> projected_names = bind.original_names;
	vector<LogicalType> projected_types = bind.original_types;
	vector<ub2> projected_oci_types = bind.oci_types;
	vector<ub4> projected_oci_sizes = bind.oci_sizes;

	if (!get.projection_ids.empty()) {
		projected_names.clear();
		projected_types.clear();
		projected_oci_types.clear();
		projected_oci_sizes.clear();
		projected_names.reserve(get.projection_ids.size());
		projected_types.reserve(get.projection_ids.size());
		for (auto idx : get.projection_ids) {
			if (idx >= bind.original_names.size()) {
				continue;
			}
			projected_names.push_back(bind.original_names[idx]);
			projected_types.push_back(bind.original_types[idx]);
			projected_oci_types.push_back(bind.oci_types[idx]);
			projected_oci_sizes.push_back(bind.oci_sizes[idx]);
		}
		get.names = projected_names;
		get.returned_types = projected_types;
	}

	vector<string> select_list;
	select_list.reserve(projected_names.size());
	for (auto &n : projected_names) {
		select_list.push_back(ColumnRefSQL(n));
	}
	auto select_sql = StringUtil::Join(select_list, ", ");

	bind.column_names = projected_names;
	bind.oci_types = projected_oci_types;
	bind.oci_sizes = projected_oci_sizes;

	bind.query = "SELECT " + select_sql + " FROM (" + bind.base_query + ")" + where_sql;
	if (bind.settings.debug_show_queries) {
		fprintf(stderr, "[oracle] pushdown query: %s\n", bind.query.c_str());
	}
	expressions = std::move(remaining);
}

} // namespace duckdb
