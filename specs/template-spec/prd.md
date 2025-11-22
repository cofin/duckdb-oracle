# Feature: [Feature Name]

**Created**: [Date]
**Status**: Planning

## Overview

[2-3 paragraphs with depth - explain WHAT, WHY, WHO benefits]

## Problem Statement

[Deep analysis of the problem - 3-4 paragraphs minimum]

## Goals

**Primary**: [Main objective with measurable success criteria]
**Secondary**: [Additional objectives]

## Acceptance Criteria

- [ ] Criterion 1: [Specific, measurable, testable]
- [ ] Criterion 2: [Specific, measurable, testable]

## Technical Design

### Technology Stack

- Language: C++
- Framework: DuckDB Extension

### Affected Components

- Modules: `src/oracle_extension.cpp`
- Tests: `test/sql/`

### Implementation Approach

[5-10 paragraphs of technical detail:
- Overall architecture
- Key algorithms/patterns
- Data flow
- Error handling
]

### C++ Signatures

```cpp
// Expected function/method signatures with full type annotations
```

## Testing Strategy

### SQL Tests

- Test file: `test/sql/test_[feature].test`
- Key scenarios:
  * [Scenario 1]
  * [Scenario 2]

### Edge Cases

- NULL value handling
- Empty result sets
- Error conditions

## Risks & Constraints

- **Risk 1**: [Description]
  * **Impact**: [High/Medium/Low]
  * **Mitigation**: [Specific mitigation strategy]

## References

- **Internal**:
  * Architecture: specs/guides/architecture.md
  * Testing: specs/guides/testing.md
  * Code Style: specs/guides/code-style.md
- **External**:
  * [DuckDB Documentation Link]
