# Changelog

All notable changes to Memphis will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Project skeleton with headers and stubs
- Build system (CMake + Makefile)
- Test framework integration (CUnit)
- Documentation (ARCHITECTURE, API, GETTING_STARTED)
- Code style enforcement (.clang-format)
- Comprehensive security configuration (TLS, privilege dropping, core dump protection)
- Input validation for JSON messages (version, string length bounds)
- DNS resolution support (getaddrinfo) for Liquidsoap connections
- Signal handling with sigaction and SIGPIPE protection

### Security
- Fixed memory corruption in log level override (ISSUE-004)
- Removed hardcoded RabbitMQ credentials, enforce strong password requirement (ISSUE-002)
- Added O_NOFOLLOW protection for log files, prevented symlink attacks (ISSUE-005)
- Implemented JSON escaping in logs, prevented log injection (ISSUE-006)
- Applied socket timeouts and TCP optimizations (ISSUE-007)
- Validated Liquidsoap command arguments, prevented injection attacks (ISSUE-001)
- Added TLS configuration support for RabbitMQ connections (ISSUE-003)
- Migrated malloc to calloc, prevented uninitialized memory bugs (ISSUE-009)
- Disabled core dumps to protect credentials from disclosure (ISSUE-010)
- Fixed host string handling, prevented use-after-free (ISSUE-012)
- Improved signal handling with sigaction, fixed SIGPIPE (ISSUE-013/014)
- Implemented privilege dropping support (ISSUE-015)
- Added build hardening flags (PIE, RELRO, stack protector, FORTIFY_SOURCE)

### Planned
- Phase 1.1: TCP socket persistent connection
- Phase 1.2: RabbitMQ consumer loop
- Phase 1.3: Event routing (skip, shutdown, announce)
- Phase 1.4: End-to-end integration test

## [0.1.0] — 2026-04-20

### Added
- Initial project structure
- Documentation framework
- Build infrastructure
- Test skeleton

---

## Release Notes Format

```
## [VERSION] — YYYY-MM-DD

### Added
- New features

### Changed
- Changes to existing functionality

### Fixed
- Bug fixes

### Removed
- Removed features

### Deprecated
- Soon-to-be removed features
```
