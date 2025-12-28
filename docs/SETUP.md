# Setup Guide

## Automated Workflow Updates

To enable the `duckdb-update-check.yml` workflow to automatically create branches and PRs for DuckDB version updates, you must configure a Personal Access Token (PAT).

### 1. Create a PAT

1.  Go to GitHub Settings > Developer settings > Personal access tokens > Tokens (classic).
2.  Generate new token (classic).
3.  Name: `DuckDB Update Bot` (or similar).
4.  Scopes:
    *   `repo` (Full control of private repositories)
    *   `workflow` (Update GitHub Action workflows)
5.  Generate token and copy it.

### 2. Add Secret to Repository

1.  Go to your repository Settings > Secrets and variables > Actions.
2.  New repository secret.
3.  Name: `WORKFLOW_UPDATE_TOKEN`
4.  Value: Paste your PAT.
5.  Add Secret.

### Why is this needed?

The default `GITHUB_TOKEN` used by Actions does not have permission to modify files within the `.github/workflows/` directory. This security restriction prevents workflows from maliciously altering other workflows. However, for our version update bot to work (which updates `main-distribution-pipeline.yml`), we need these elevated permissions.
