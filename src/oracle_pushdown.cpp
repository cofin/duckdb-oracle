#include "oracle_pushdown.hpp"
#include "oracle_table_function.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_between_expression.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
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

static bool StringConstantValue(Expression &expr, string &out_value) {
	if (expr.type != ExpressionType::VALUE_CONSTANT) {
		return false;
	}
	auto &constant = expr.Cast<BoundConstantExpression>();
	if (constant.value.IsNull() || constant.value.type().id() != LogicalTypeId::VARCHAR) {
		return false;
	}
	out_value = constant.value.GetValue<string>();
	return true;
}

static string EscapeOracleLikePattern(const string &pattern) {
	string escaped;
	escaped.reserve(pattern.size());
	for (auto c : pattern) {
		if (c == '\\' || c == '%' || c == '_') {
			escaped += '\\';
		}
		escaped += c;
	}
	return escaped;
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

static bool IsColumnRef(Expression *expr) {
	return expr->type == ExpressionType::BOUND_REF || expr->type == ExpressionType::BOUND_COLUMN_REF;
}

static idx_t GetColumnIndex(Expression *expr, const vector<ColumnIndex> &column_ids) {
	if (expr->type == ExpressionType::BOUND_REF) {
		return ResolveBoundRefColumnIndex(expr->Cast<BoundReferenceExpression>().index, column_ids);
	}
	if (expr->type == ExpressionType::BOUND_COLUMN_REF) {
		return ResolveBoundRefColumnIndex(expr->Cast<BoundColumnRefExpression>().binding.column_index, column_ids);
	}
	return DConstants::INVALID_INDEX;
}

static bool CanPushdownColumn(idx_t col_idx, const vector<string> &names, const vector<bool> &pushdown_eligible) {
	return col_idx != DConstants::INVALID_INDEX && col_idx < names.size() && col_idx < pushdown_eligible.size() &&
	       pushdown_eligible[col_idx];
}

static bool CanPushdownLike(idx_t col_idx, const vector<string> &oracle_type_names) {
	if (col_idx >= oracle_type_names.size()) {
		return false;
	}
	auto oracle_type = StringUtil::Upper(oracle_type_names[col_idx]);
	return oracle_type == "VARCHAR2" || oracle_type == "NVARCHAR2" || oracle_type == "VARCHAR";
}

struct OraclePushdownPlan {
	vector<unique_ptr<Expression>> residual_expressions;
	vector<string> clauses;
	vector<string> projected_names;
	vector<LogicalType> projected_types;
	vector<ub2> projected_oci_types;
	vector<ub4> projected_oci_sizes;
	vector<string> projected_oracle_type_names;
	vector<bool> projected_pushdown_eligible;
	string query;

	string WhereSQL() const {
		if (clauses.empty()) {
			return string();
		}
		return " WHERE " + StringUtil::Join(clauses, " AND ");
	}

	string ProjectedSelectListSQL() const {
		vector<string> select_list;
		select_list.reserve(projected_names.size());
		for (auto &name : projected_names) {
			select_list.push_back(ColumnRefSQL(name));
		}
		return StringUtil::Join(select_list, ", ");
	}

	void BuildQuery(const OracleBindData &bind) {
		auto projected_select_list = ProjectedSelectListSQL();
		if (!bind.direct_from_sql.empty() && !bind.direct_select_list_sql.empty()) {
			auto inner_query = "SELECT " + bind.direct_select_list_sql + " FROM " + bind.direct_from_sql + WhereSQL();
			query = "SELECT " + projected_select_list + " FROM (" + inner_query + ")";
			return;
		}
		query = "SELECT " + projected_select_list + " FROM (" + bind.base_query + ")" + WhereSQL();
	}
};

static bool TryExtractComparison(Expression &expr, const vector<string> &names, const vector<ColumnIndex> &column_ids,
                                 const vector<bool> &pushdown_eligible, string &out_clause) {
	if (expr.type != ExpressionType::COMPARE_EQUAL && expr.type != ExpressionType::COMPARE_LESSTHAN &&
	    expr.type != ExpressionType::COMPARE_GREATERTHAN && expr.type != ExpressionType::COMPARE_LESSTHANOREQUALTO &&
	    expr.type != ExpressionType::COMPARE_GREATERTHANOREQUALTO && expr.type != ExpressionType::COMPARE_NOTEQUAL) {
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

	idx_t col_idx = DConstants::INVALID_INDEX;
	if (IsColumnRef(cmp.left.get()) && cmp.right->type == ExpressionType::VALUE_CONSTANT) {
		col_idx = GetColumnIndex(cmp.left.get(), column_ids);
		const_expr = cmp.right.get();
	} else if (IsColumnRef(cmp.right.get()) && cmp.left->type == ExpressionType::VALUE_CONSTANT) {
		col_idx = GetColumnIndex(cmp.right.get(), column_ids);
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

	if (!CanPushdownColumn(col_idx, names, pushdown_eligible)) {
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
	if (const_sql == "NULL") {
		return false;
	}

	string op;
	switch (op_type) {
	case ExpressionType::COMPARE_EQUAL:
		op = "=";
		break;
	case ExpressionType::COMPARE_NOTEQUAL:
		op = "<>";
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

static bool TryExtractBetween(Expression &expr, const vector<string> &names, const vector<ColumnIndex> &column_ids,
                              const vector<bool> &pushdown_eligible, string &out_clause) {
	if (expr.GetExpressionClass() != ExpressionClass::BOUND_BETWEEN) {
		return false;
	}
	auto &between = expr.Cast<BoundBetweenExpression>();
	if (!IsColumnRef(between.input.get()) || between.lower->type != ExpressionType::VALUE_CONSTANT ||
	    between.upper->type != ExpressionType::VALUE_CONSTANT) {
		return false;
	}
	auto col_idx = GetColumnIndex(between.input.get(), column_ids);
	if (!CanPushdownColumn(col_idx, names, pushdown_eligible)) {
		return false;
	}
	string lower_sql;
	string upper_sql;
	if (!ConstantToSQL(*between.lower, lower_sql) || !ConstantToSQL(*between.upper, upper_sql)) {
		return false;
	}
	if (lower_sql == "NULL" || upper_sql == "NULL") {
		return false;
	}

	auto column = ColumnRefSQL(names[col_idx]);
	if (between.lower_inclusive && between.upper_inclusive) {
		out_clause = StringUtil::Format("%s BETWEEN %s AND %s", column.c_str(), lower_sql.c_str(), upper_sql.c_str());
	} else {
		auto lower_op = between.lower_inclusive ? ">=" : ">";
		auto upper_op = between.upper_inclusive ? "<=" : "<";
		out_clause = StringUtil::Format("%s %s %s AND %s %s %s", column.c_str(), lower_op, lower_sql.c_str(),
		                                column.c_str(), upper_op, upper_sql.c_str());
	}
	return true;
}

static bool TryExtractIsNull(Expression &expr, const vector<string> &names, const vector<ColumnIndex> &column_ids,
                             const vector<bool> &pushdown_eligible, string &out_clause) {
	if (expr.type != ExpressionType::OPERATOR_IS_NULL && expr.type != ExpressionType::OPERATOR_IS_NOT_NULL) {
		return false;
	}
	auto &op = expr.Cast<BoundOperatorExpression>();
	if (op.children.size() != 1) {
		return false;
	}

	idx_t col_idx = DConstants::INVALID_INDEX;
	if (IsColumnRef(op.children[0].get())) {
		col_idx = GetColumnIndex(op.children[0].get(), column_ids);
	} else {
		return false;
	}

	if (!CanPushdownColumn(col_idx, names, pushdown_eligible)) {
		return false;
	}
	out_clause =
	    ColumnRefSQL(names[col_idx]) + (expr.type == ExpressionType::OPERATOR_IS_NULL ? " IS NULL" : " IS NOT NULL");
	return true;
}

static bool TryExtractIn(Expression &expr, const vector<string> &names, const vector<ColumnIndex> &column_ids,
                         const vector<bool> &pushdown_eligible, string &out_clause) {
	if (expr.type != ExpressionType::COMPARE_IN) {
		return false;
	}
	auto &op = expr.Cast<BoundOperatorExpression>();
	if (op.children.size() < 2 || op.children.size() > 101 || !IsColumnRef(op.children[0].get())) {
		return false;
	}
	auto col_idx = GetColumnIndex(op.children[0].get(), column_ids);
	if (!CanPushdownColumn(col_idx, names, pushdown_eligible)) {
		return false;
	}
	vector<string> constants;
	constants.reserve(op.children.size() - 1);
	for (idx_t i = 1; i < op.children.size(); i++) {
		if (op.children[i]->type != ExpressionType::VALUE_CONSTANT) {
			return false;
		}
		string constant_sql;
		if (!ConstantToSQL(*op.children[i], constant_sql) || constant_sql == "NULL") {
			return false;
		}
		constants.push_back(std::move(constant_sql));
	}
	out_clause = StringUtil::Format("%s IN (%s)", ColumnRefSQL(names[col_idx]).c_str(),
	                                StringUtil::Join(constants, ", ").c_str());
	return true;
}

static bool TryExtractLike(Expression &expr, const vector<string> &names, const vector<ColumnIndex> &column_ids,
                           const vector<bool> &pushdown_eligible, const vector<string> &oracle_type_names,
                           string &out_clause) {
	if (expr.GetExpressionClass() != ExpressionClass::BOUND_FUNCTION) {
		return false;
	}
	auto &fn = expr.Cast<BoundFunctionExpression>();
	auto function_name = StringUtil::Lower(fn.function.name);
	if (fn.children.empty()) {
		return false;
	}
	if (!IsColumnRef(fn.children[0].get())) {
		return false;
	}
	auto col_idx = GetColumnIndex(fn.children[0].get(), column_ids);
	if (!CanPushdownColumn(col_idx, names, pushdown_eligible) || !CanPushdownLike(col_idx, oracle_type_names)) {
		return false;
	}

	if (function_name == "~~" && fn.children.size() == 2) {
		string pattern_sql;
		if (!ConstantToSQL(*fn.children[1], pattern_sql) || pattern_sql == "NULL") {
			return false;
		}
		out_clause = StringUtil::Format("%s LIKE %s", ColumnRefSQL(names[col_idx]).c_str(), pattern_sql.c_str());
		return true;
	}

	if (function_name == "like_escape" && fn.children.size() == 3) {
		string pattern_sql;
		string escape_value;
		if (!ConstantToSQL(*fn.children[1], pattern_sql) || !StringConstantValue(*fn.children[2], escape_value) ||
		    pattern_sql == "NULL" || escape_value.size() != 1) {
			return false;
		}
		out_clause = StringUtil::Format("%s LIKE %s ESCAPE %s", ColumnRefSQL(names[col_idx]).c_str(),
		                                pattern_sql.c_str(), OracleQuoteStringLiteral(escape_value).c_str());
		return true;
	}

	if ((function_name == "prefix" || function_name == "suffix" || function_name == "contains") &&
	    fn.children.size() == 2) {
		string pattern;
		if (!StringConstantValue(*fn.children[1], pattern)) {
			return false;
		}
		auto escaped = EscapeOracleLikePattern(pattern);
		if (function_name == "prefix") {
			escaped += "%";
		} else if (function_name == "suffix") {
			escaped = "%" + escaped;
		} else {
			escaped = "%" + escaped + "%";
		}
		auto escape_sql = OracleQuoteStringLiteral("\\");
		out_clause = StringUtil::Format("%s LIKE %s ESCAPE %s", ColumnRefSQL(names[col_idx]).c_str(),
		                                OracleQuoteStringLiteral(escaped).c_str(), escape_sql.c_str());
		return true;
	}

	return false;
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

	OraclePushdownPlan plan;
	plan.clauses = bind.pushdown_clauses;
	for (auto &expr : expressions) {
		string clause;
		if (TryExtractComparison(*expr, bind.column_names, get.GetColumnIds(), bind.pushdown_eligible, clause) ||
		    TryExtractBetween(*expr, bind.column_names, get.GetColumnIds(), bind.pushdown_eligible, clause) ||
		    TryExtractIsNull(*expr, bind.column_names, get.GetColumnIds(), bind.pushdown_eligible, clause) ||
		    TryExtractIn(*expr, bind.column_names, get.GetColumnIds(), bind.pushdown_eligible, clause) ||
		    TryExtractLike(*expr, bind.column_names, get.GetColumnIds(), bind.pushdown_eligible, bind.oracle_type_names,
		                   clause)) {
			if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
				fprintf(stderr, "[oracle] pushdown: extracted clause: %s\n", clause.c_str());
			}
			plan.clauses.push_back(std::move(clause));
			continue;
		}
		if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] pushdown: could not extract expression type=%d\n", (int)expr->type);
		}
		plan.residual_expressions.push_back(std::move(expr));
	}

	plan.projected_names = bind.original_names;
	plan.projected_types = bind.original_types;
	plan.projected_oci_types = bind.oci_types;
	plan.projected_oci_sizes = bind.oci_sizes;
	plan.projected_oracle_type_names = bind.oracle_type_names;
	plan.projected_pushdown_eligible = bind.pushdown_eligible;

	if (!get.projection_ids.empty()) {
		plan.projected_names.clear();
		plan.projected_types.clear();
		plan.projected_oci_types.clear();
		plan.projected_oci_sizes.clear();
		plan.projected_oracle_type_names.clear();
		plan.projected_pushdown_eligible.clear();
		plan.projected_names.reserve(get.projection_ids.size());
		plan.projected_types.reserve(get.projection_ids.size());
		plan.projected_oracle_type_names.reserve(get.projection_ids.size());
		plan.projected_pushdown_eligible.reserve(get.projection_ids.size());
		for (auto idx : get.projection_ids) {
			if (idx >= bind.original_names.size()) {
				continue;
			}
			plan.projected_names.push_back(bind.original_names[idx]);
			plan.projected_types.push_back(bind.original_types[idx]);
			plan.projected_oci_types.push_back(bind.oci_types[idx]);
			plan.projected_oci_sizes.push_back(bind.oci_sizes[idx]);
			if (idx < bind.oracle_type_names.size()) {
				plan.projected_oracle_type_names.push_back(bind.oracle_type_names[idx]);
			} else {
				plan.projected_oracle_type_names.push_back("unknown Oracle type");
			}
			plan.projected_pushdown_eligible.push_back(idx < bind.pushdown_eligible.size() &&
			                                           bind.pushdown_eligible[idx]);
		}
		get.names = plan.projected_names;
		get.returned_types = plan.projected_types;
	}

	plan.BuildQuery(bind);

	bind.column_names = plan.projected_names;
	bind.oci_types = plan.projected_oci_types;
	bind.oci_sizes = plan.projected_oci_sizes;
	bind.oracle_type_names = plan.projected_oracle_type_names;
	bind.pushdown_eligible = plan.projected_pushdown_eligible;
	bind.pushdown_clauses = plan.clauses;

	bind.query = plan.query;
	if (bind.settings.debug_show_queries) {
		fprintf(stderr, "[oracle] pushdown query: %s\n", bind.query.c_str());
	}
	expressions = std::move(plan.residual_expressions);
}

} // namespace duckdb
