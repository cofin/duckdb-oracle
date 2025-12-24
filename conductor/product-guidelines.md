# Product Guidelines

## Communication Tone & Voice
- **Professional & Authoritative:** All technical communication, documentation, and error messages must be clear, precise, and expert-led.
- **Accuracy First:** Focus on technical accuracy and robust engineering principles to reflect the "production-grade" nature of the extension.
- **Directness:** Avoid fluff; provide clear, direct instructions and explanations.

## Error Handling & Feedback
- **Diagnostic & Actionable:** Error messages should provide enough detail to identify the source of the problem in either DuckDB or the Oracle OCI layer.
- **Resolution-Focused:** Whenever possible, include clear steps or suggestions for resolution (e.g., "Set ORACLE_HOME" or "Verify TNS_ADMIN").
- **Graceful Failure:** Ensure that failures are caught and reported clearly. The extension must never crash or produce uncaught segfaults; it should always return a descriptive error message to the DuckDB shell.
- **Simplification with Purpose:** While detail is preferred, complex OCI errors should be summarized into a more user-friendly context if it aids understanding without losing critical diagnostic info.

## Configuration & Defaults
- **Convention over Configuration:** Use smart defaults to minimize user friction. The extension should proactively detect the environment (e.g., `ORACLE_HOME`, `TNS_ADMIN`, current schemas, and Oracle Wallets).
- **Adaptive/Hybrid Control:** While defaults should handle most cases, provide granular and explicit configuration options for power users to tune performance (e.g., prefetch memory, array sizes) or handle specific architectural constraints.
- **Intuitive Integration:** Any configuration options should feel native to the environment they target—leveraging DuckDB's `SET` syntax and Oracle's established connection string conventions.

## Visual Identity & Documentation
- **Technical & Clean:** Documentation should favor high-quality, standard technical diagrams (ERDs, sequence diagrams) and a clean, monospaced-friendly layout for code examples.
- **Readability Priority:** Visual elements must prioritize clarity and readability over stylistic flair, ensuring that the documentation serves as a reliable technical reference.

## Stability & Reliability Protocol
- **Zero-Regression Commitment:** Stability is a core feature. Every new feature or fix must be accompanied by comprehensive integration tests.
- **Real-World Validation:** Testing must include scenarios against containerized Oracle instances (e.g., Oracle Free/XE) to ensure the extension behaves correctly under real conditions.
- **Continuous Quality:** Adhere to DuckDB's internal coding standards (`make format`, `make tidy`) to ensure long-term maintainability and codebase health.
