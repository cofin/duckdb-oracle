#include "oracle_pushdown.hpp"
#include "oracle_table_function.hpp"
#include "oracle_utils.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/interval.hpp"
#include "duckdb/common/types/time.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_between_expression.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/planner/expression/bound_operator_expression.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"
#include <cmath>
#include <cstdio>

namespace duckdb {

static string ColumnRefSQL(const string &col_name) {
	return KeywordHelper::WriteQuoted(col_name, '"');
}

static string OracleQuoteStringLiteral(const string &value) {
	return "'" + StringUtil::Replace(value, "'", "''") + "'";
}

static bool ConstantToSQL(const Expression &expr, string &out_sql, const string &oracle_type_name = "") {
	if (expr.GetExpressionType() != ExpressionType::VALUE_CONSTANT) {
		return false;
	}
	auto &c = expr.Cast<BoundConstantExpression>();
	const auto &val = c.GetValue();
	if (val.IsNull()) {
		out_sql = "NULL";
		return true;
	}

	switch (val.type().id()) {
	case LogicalTypeId::VARCHAR: {
		auto str_val = StringValue::Get(val);
		if (str_val.empty()) {
			return false;
		}
		out_sql = OracleQuoteStringLiteral(str_val);
		return true;
	}
	case LogicalTypeId::DATE: {
		auto date = DateValue::Get(val);
		if (!date.IsFinite()) {
			return false;
		}
		int32_t year, month, day;
		Date::Convert(date, year, month, day);
		if (year < 1 || year > 9999) {
			return false;
		}
		out_sql = "DATE " + OracleQuoteStringLiteral(Date::Format(year, month, day));
		return true;
	}
	case LogicalTypeId::FLOAT:
	case LogicalTypeId::DOUBLE: {
		auto dval = val.GetValue<double>();
		if (!std::isfinite(dval)) {
			return false;
		}
		out_sql = val.ToString();
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
	case LogicalTypeId::DECIMAL:
		out_sql = val.ToString();
		return true;
	case LogicalTypeId::TIMESTAMP_TZ:
		return false;
	case LogicalTypeId::TIMESTAMP:
	case LogicalTypeId::TIMESTAMP_SEC:
	case LogicalTypeId::TIMESTAMP_MS:
	case LogicalTypeId::TIMESTAMP_NS: {
		auto ts_value = val.type().id() == LogicalTypeId::TIMESTAMP ? val : val.DefaultCastAs(LogicalType::TIMESTAMP);
		auto ts_val = TimestampValue::Get(ts_value);
		if (!ts_val.IsFinite()) {
			return false;
		}
		date_t date_part;
		dtime_t time_part;
		Timestamp::Convert(ts_val, date_part, time_part);
		int32_t year, month, day;
		Date::Convert(date_part, year, month, day);
		if (year < 1 || year > 9999) {
			return false;
		}
		if (StringUtil::Upper(oracle_type_name) == "DATE" && (time_part.value % Interval::MICROS_PER_SEC == 0)) {
			int32_t hour, min, sec, micros;
			Time::Convert(time_part, hour, min, sec, micros);
			out_sql = StringUtil::Format("TO_DATE('%s %02d:%02d:%02d', 'YYYY-MM-DD HH24:MI:SS')",
			                             Date::Format(year, month, day).c_str(), hour, min, sec);
			return true;
		}
		out_sql = "TIMESTAMP " + OracleQuoteStringLiteral(Timestamp::ToString(ts_val));
		return true;
	}
	default:
		return false;
	}
}

static bool StringConstantValue(const Expression &expr, string &out_value) {
	if (expr.GetExpressionType() != ExpressionType::VALUE_CONSTANT) {
		return false;
	}
	auto &constant = expr.Cast<BoundConstantExpression>();
	const auto &val = constant.GetValue();
	if (val.IsNull() || val.type().id() != LogicalTypeId::VARCHAR) {
		return false;
	}
	out_value = val.GetValue<string>();
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

static bool IsColumnRef(const Expression *expr) {
	return expr->GetExpressionType() == ExpressionType::BOUND_REF ||
	       expr->GetExpressionType() == ExpressionType::BOUND_COLUMN_REF;
}

static idx_t GetColumnIndex(const Expression *expr, const vector<ColumnIndex> &column_ids) {
	if (expr->GetExpressionType() == ExpressionType::BOUND_REF) {
		return ResolveBoundRefColumnIndex(expr->Cast<BoundReferenceExpression>().Index(), column_ids);
	}
	if (expr->GetExpressionType() == ExpressionType::BOUND_COLUMN_REF) {
		return ResolveBoundRefColumnIndex(expr->Cast<BoundColumnRefExpression>().Binding().column_index, column_ids);
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
	return oracle_type == "VARCHAR2" || oracle_type == "NVARCHAR2" || oracle_type == "VARCHAR" ||
	       oracle_type == "OCI CHARACTER";
}

struct OraclePushdownPlan {
	vector<unique_ptr<Expression>> residual_expressions;
	vector<string> clauses;
	vector<string> projected_names;
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

	string BuildQuery(const OracleBindData &bind) {
		if (!bind.direct_from_sql.empty() && !bind.direct_select_list_sql.empty()) {
			query = "SELECT " + bind.direct_select_list_sql + " FROM " + bind.direct_from_sql + WhereSQL();
			return query;
		}
		if (clauses.empty()) {
			query = bind.base_query;
			return query;
		}
		auto projected_select_list = ProjectedSelectListSQL();
		query = "SELECT " + projected_select_list + " FROM (\n" + bind.base_query + "\n)" + WhereSQL();
		return query;
	}
};

static bool TryExtractComparison(Expression &expr, const vector<string> &names, const vector<ColumnIndex> &column_ids,
                                 const vector<bool> &pushdown_eligible, const vector<string> &oracle_type_names,
                                 string &out_clause) {
	auto expr_type = expr.GetExpressionType();
	if (expr_type != ExpressionType::COMPARE_EQUAL && expr_type != ExpressionType::COMPARE_LESSTHAN &&
	    expr_type != ExpressionType::COMPARE_GREATERTHAN && expr_type != ExpressionType::COMPARE_LESSTHANOREQUALTO &&
	    expr_type != ExpressionType::COMPARE_GREATERTHANOREQUALTO && expr_type != ExpressionType::COMPARE_NOTEQUAL) {
		if (getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] TryExtractComparison: not a comparison, type=%d\n", (int)expr_type);
		}
		return false;
	}
	if (expr.GetExpressionClass() != ExpressionClass::BOUND_FUNCTION) {
		return false;
	}
	auto &cmp = expr.Cast<BoundFunctionExpression>();
	const auto &left = BoundComparisonExpression::Left(cmp);
	const auto &right = BoundComparisonExpression::Right(cmp);
	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] TryExtractComparison: left_type=%d, right_type=%d\n",
		        (int)left.GetExpressionType(), (int)right.GetExpressionType());
	}
	const Expression *const_expr = nullptr;
	ExpressionType op_type = expr_type;

	idx_t col_idx = DConstants::INVALID_INDEX;
	if (IsColumnRef(&left) && right.GetExpressionType() == ExpressionType::VALUE_CONSTANT) {
		col_idx = GetColumnIndex(&left, column_ids);
		const_expr = &right;
	} else if (IsColumnRef(&right) && left.GetExpressionType() == ExpressionType::VALUE_CONSTANT) {
		col_idx = GetColumnIndex(&right, column_ids);
		const_expr = &left;
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
	auto oracle_type = col_idx < oracle_type_names.size() ? oracle_type_names[col_idx] : string();
	string const_sql;
	if (!ConstantToSQL(*const_expr, const_sql, oracle_type)) {
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
                              const vector<bool> &pushdown_eligible, const vector<string> &oracle_type_names,
                              string &out_clause) {
	if (expr.GetExpressionType() != ExpressionType::COMPARE_BETWEEN ||
	    expr.GetExpressionClass() != ExpressionClass::BOUND_FUNCTION) {
		return false;
	}
	auto &between = expr.Cast<BoundFunctionExpression>();
	const auto &input = BoundBetweenExpression::Input(between);
	const auto &lower = BoundBetweenExpression::LowerBound(between);
	const auto &upper = BoundBetweenExpression::UpperBound(between);
	if (!IsColumnRef(&input) || lower.GetExpressionType() != ExpressionType::VALUE_CONSTANT ||
	    upper.GetExpressionType() != ExpressionType::VALUE_CONSTANT) {
		return false;
	}
	auto col_idx = GetColumnIndex(&input, column_ids);
	if (!CanPushdownColumn(col_idx, names, pushdown_eligible)) {
		return false;
	}
	auto oracle_type = col_idx < oracle_type_names.size() ? oracle_type_names[col_idx] : string();
	string lower_sql;
	string upper_sql;
	if (!ConstantToSQL(lower, lower_sql, oracle_type) || !ConstantToSQL(upper, upper_sql, oracle_type)) {
		return false;
	}
	if (lower_sql == "NULL" || upper_sql == "NULL") {
		return false;
	}

	auto column = ColumnRefSQL(names[col_idx]);
	bool lower_inclusive = BoundBetweenExpression::LowerInclusive(between);
	bool upper_inclusive = BoundBetweenExpression::UpperInclusive(between);
	if (lower_inclusive && upper_inclusive) {
		out_clause = StringUtil::Format("%s BETWEEN %s AND %s", column.c_str(), lower_sql.c_str(), upper_sql.c_str());
	} else {
		auto lower_op = lower_inclusive ? ">=" : ">";
		auto upper_op = upper_inclusive ? "<=" : "<";
		out_clause = StringUtil::Format("%s %s %s AND %s %s %s", column.c_str(), lower_op, lower_sql.c_str(),
		                                column.c_str(), upper_op, upper_sql.c_str());
	}
	return true;
}

static bool TryExtractIsNull(Expression &expr, const vector<string> &names, const vector<ColumnIndex> &column_ids,
                             const vector<bool> &pushdown_eligible, string &out_clause) {
	auto expr_type = expr.GetExpressionType();
	if (expr_type != ExpressionType::OPERATOR_IS_NULL && expr_type != ExpressionType::OPERATOR_IS_NOT_NULL) {
		return false;
	}
	auto &op = expr.Cast<BoundOperatorExpression>();
	const auto &children = op.GetChildren();
	if (children.size() != 1) {
		return false;
	}

	idx_t col_idx = DConstants::INVALID_INDEX;
	if (IsColumnRef(children[0].get())) {
		col_idx = GetColumnIndex(children[0].get(), column_ids);
	} else {
		return false;
	}

	if (!CanPushdownColumn(col_idx, names, pushdown_eligible)) {
		return false;
	}
	out_clause =
	    ColumnRefSQL(names[col_idx]) + (expr_type == ExpressionType::OPERATOR_IS_NULL ? " IS NULL" : " IS NOT NULL");
	return true;
}

static bool TryExtractIn(Expression &expr, const vector<string> &names, const vector<ColumnIndex> &column_ids,
                         const vector<bool> &pushdown_eligible, const vector<string> &oracle_type_names,
                         string &out_clause) {
	if (expr.GetExpressionType() != ExpressionType::COMPARE_IN) {
		return false;
	}
	auto &op = expr.Cast<BoundOperatorExpression>();
	const auto &children = op.GetChildren();
	if (children.size() < 2 || children.size() > 101 || !IsColumnRef(children[0].get())) {
		return false;
	}
	auto col_idx = GetColumnIndex(children[0].get(), column_ids);
	if (!CanPushdownColumn(col_idx, names, pushdown_eligible)) {
		return false;
	}
	auto oracle_type = col_idx < oracle_type_names.size() ? oracle_type_names[col_idx] : string();
	vector<string> constants;
	constants.reserve(children.size() - 1);
	for (idx_t i = 1; i < children.size(); i++) {
		if (children[i]->GetExpressionType() != ExpressionType::VALUE_CONSTANT) {
			return false;
		}
		string constant_sql;
		if (!ConstantToSQL(*children[i], constant_sql, oracle_type) || constant_sql == "NULL") {
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
	auto function_name = StringUtil::Lower(fn.Function().GetName().GetIdentifierName());
	const auto &children = fn.GetChildren();
	if (children.empty()) {
		return false;
	}
	if (!IsColumnRef(children[0].get())) {
		return false;
	}
	auto col_idx = GetColumnIndex(children[0].get(), column_ids);
	if (!CanPushdownColumn(col_idx, names, pushdown_eligible) || !CanPushdownLike(col_idx, oracle_type_names)) {
		return false;
	}

	if (function_name == "~~" && children.size() == 2) {
		string pattern_sql;
		if (!ConstantToSQL(*children[1], pattern_sql) || pattern_sql == "NULL") {
			return false;
		}
		out_clause = StringUtil::Format("%s LIKE %s", ColumnRefSQL(names[col_idx]).c_str(), pattern_sql.c_str());
		return true;
	}

	if (function_name == "like_escape" && children.size() == 3) {
		string pattern_sql;
		string escape_value;
		if (!ConstantToSQL(*children[1], pattern_sql) || !StringConstantValue(*children[2], escape_value) ||
		    pattern_sql == "NULL" || escape_value.size() != 1) {
			return false;
		}
		out_clause = StringUtil::Format("%s LIKE %s ESCAPE %s", ColumnRefSQL(names[col_idx]).c_str(),
		                                pattern_sql.c_str(), OracleQuoteStringLiteral(escape_value).c_str());
		return true;
	}

	if ((function_name == "prefix" || function_name == "suffix" || function_name == "contains") &&
	    children.size() == 2) {
		string pattern;
		if (!StringConstantValue(*children[1], pattern) || pattern.empty()) {
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
		if (TryExtractComparison(*expr, bind.column_names, get.GetColumnIds(), bind.pushdown_eligible,
		                         bind.oracle_type_names, clause) ||
		    TryExtractBetween(*expr, bind.column_names, get.GetColumnIds(), bind.pushdown_eligible,
		                      bind.oracle_type_names, clause) ||
		    TryExtractIsNull(*expr, bind.column_names, get.GetColumnIds(), bind.pushdown_eligible, clause) ||
		    TryExtractIn(*expr, bind.column_names, get.GetColumnIds(), bind.pushdown_eligible, bind.oracle_type_names,
		                 clause) ||
		    TryExtractLike(*expr, bind.column_names, get.GetColumnIds(), bind.pushdown_eligible, bind.oracle_type_names,
		                   clause)) {
			if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
				fprintf(stderr, "[oracle] pushdown: extracted clause: %s\n", clause.c_str());
			}
			plan.clauses.push_back(std::move(clause));
			continue;
		}
		if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] pushdown: could not extract expression type=%d\n",
			        (int)expr->GetExpressionType());
		}
		plan.residual_expressions.push_back(std::move(expr));
	}

	plan.projected_names = bind.original_names;
	bind.pushdown_clauses = plan.clauses;
	bind.query = plan.BuildQuery(bind);
	if (bind.settings.debug_show_queries) {
		fprintf(stderr, "[oracle] pushdown query: %s\n", bind.query.c_str());
	}
	expressions = std::move(plan.residual_expressions);
}

} // namespace duckdb
