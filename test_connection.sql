LOAD 'build/release/extension/oracle/oracle.duckdb_extension';
SELECT * FROM oracle_query('duckdb_test/duckdb_test@//localhost:11521/FREEPDB1', 'SELECT 1 FROM DUAL');
SELECT * FROM oracle_scan('duckdb_test/duckdb_test@//localhost:11521/FREEPDB1', 'DUCKDB_TEST', 'TEST_TYPES');
