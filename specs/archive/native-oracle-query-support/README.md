# Template Workspace Structure

This directory serves as a template for new feature workspaces.

## Structure

```
specs/template-spec/
├── prd.md              # Product Requirements Document template
├── tasks.md            # Implementation checklist template
├── recovery.md         # Session resumability template
├── research/           # Research findings directory
│   └── plan.md         # Research plan and findings
└── tmp/                # Temporary working files
```

## Usage

When starting a new feature:

1. Run `/prd "feature description"`
2. Gemini will copy this template structure
3. Populate with feature-specific content
4. Workspace created in `specs/active/[feature-slug]/`

## Files

- **prd.md**: Complete requirements and technical design
- **tasks.md**: Phase-by-phase implementation checklist
- **recovery.md**: Session resume instructions with current status
- **research/plan.md**: Research findings and architectural decisions
- **tmp/**: Scratch space for temporary files (cleaned before archive)
