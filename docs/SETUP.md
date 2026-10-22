# Repository Setup

## Automated Update Token

The `duckdb-update-check.yml` and `auto-tag.yml` workflows use a repository secret named `WORKFLOW_UPDATE_TOKEN` so they can update workflow files and push tags that trigger downstream release runs.

1. Create a GitHub Personal Access Token (classic) with `repo` and `workflow` scopes.
2. In the repository, go to **Settings > Secrets and variables > Actions**.
3. Create a secret named `WORKFLOW_UPDATE_TOKEN` with the token value.

## GitHub Pages (Custom Extension Repository)

The `release-unsigned.yml` workflow publishes compressed binaries (`v<duckdb_version>/<platform>/oracle.duckdb_extension.gz`) and `.nojekyll` to the `gh-pages` branch.

To serve the custom extension repository at `https://cofin.github.io/duckdb-oracle`:

1. Go to **Settings > Pages**.
2. Set **Source** to **Deploy from a branch**.
3. Select branch **`gh-pages`** and folder **`/ (root)`**, then click **Save**.
