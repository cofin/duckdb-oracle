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

GRANT CREATE SESSION TO duckdb_test;
GRANT CREATE TABLE TO duckdb_test;
GRANT CREATE VIEW TO duckdb_test;
GRANT CREATE MATERIALIZED VIEW TO duckdb_test;
GRANT CREATE SYNONYM TO duckdb_test;
GRANT CREATE TYPE TO duckdb_test;
GRANT CREATE PROCEDURE TO duckdb_test;
ALTER USER duckdb_test QUOTA UNLIMITED ON USERS;
