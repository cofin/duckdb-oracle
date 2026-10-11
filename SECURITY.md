# Security Policy

## Supported Versions

We support the latest stable release of the extension.

| Version | Supported          |
| ------- | ------------------ |
| Latest  | :white_check_mark: |
| < Latest| :x:                |

## Reporting a Vulnerability

If you discover a security vulnerability in this project, please do **not** open a public issue.

Instead, please report it privately via GitHub Security Advisories at <https://github.com/cofin/duckdb-oracle/security/advisories/new>.

We will assess the issue and respond within 48 hours.

## Oracle Instant Client Security

This extension depends on the Oracle Instant Client libraries. Security updates for these libraries are managed by Oracle. We strive to build and test against the latest available Instant Client versions to ensure upstream security patches are included.

Users are responsible for ensuring their runtime environment uses a secure version of the Oracle Instant Client libraries.
