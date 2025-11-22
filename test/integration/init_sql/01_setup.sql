-- Create a test user and grant permissions
ALTER SESSION SET CONTAINER=FREEPDB1;

-- Create user iff absent to avoid ORA-01920 when rerunning
DECLARE
    v_exists NUMBER;
BEGIN
    SELECT COUNT(*) INTO v_exists FROM dba_users WHERE username = 'DUCKDB_TEST';
    IF v_exists = 0 THEN
        EXECUTE IMMEDIATE 'CREATE USER duckdb_test IDENTIFIED BY duckdb_test';
    END IF;
END;
/

GRANT CONNECT, RESOURCE, DBA TO duckdb_test;
GRANT UNLIMITED TABLESPACE TO duckdb_test;

-- Create some test tables
BEGIN
    EXECUTE IMMEDIATE 'DROP TABLE duckdb_test.test_types PURGE';
EXCEPTION
    WHEN OTHERS THEN
        IF SQLCODE != -942 THEN
            RAISE;
        END IF;
END;
/

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
