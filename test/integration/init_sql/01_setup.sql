-- Create a test user and grant permissions
ALTER SESSION SET CONTAINER=FREEPDB1;

CREATE USER duckdb_test IDENTIFIED BY duckdb_test;
GRANT CONNECT, RESOURCE, DBA TO duckdb_test;
GRANT UNLIMITED TABLESPACE TO duckdb_test;

-- Create some test tables
CREATE TABLE duckdb_test.test_types (
    id NUMBER PRIMARY KEY,
    val_varchar VARCHAR2(100),
    val_number NUMBER(10,2),
    val_date DATE,
    val_timestamp TIMESTAMP,
    val_vector VECTOR(3, FLOAT32)
);

INSERT INTO duckdb_test.test_types VALUES (1, 'test', 123.45, SYSDATE, SYSTIMESTAMP, '[1.1, 2.2, 3.3]');
COMMIT;
