# Contributing to Memphis

Thank you for your interest in contributing to Memphis! Here's how to get started.

## Code of Conduct

Be respectful, inclusive, and constructive. We're building a friendly community.

## Development Setup

```bash
git clone https://github.com/intelliurb/memphis.git
cd memphis

# Install dependencies
sudo apt-get install -y build-essential cmake librabbitmq-dev libjansson-dev libcunit1-dev

# Build
make debug

# Run tests
make test
```

## Making Changes

### 1. Fork & Branch

```bash
git checkout -b feature/your-feature
```

### 2. Code Quality

Before committing:
```bash
make format     # Auto-format with clang-format
make lint       # Check formatting
make test       # Run all tests
make coverage   # Verify coverage
```

### 3. Commit Message Style

```
feat: add TCP keepalive to liquidsoap client

- Implement persistent connection in ls_send_command()
- Add SO_KEEPALIVE socket option
- Add 3 new tests

Closes #123
```

**Format**: `type: subject`

Types:
- `feat:` — New feature
- `fix:` — Bug fix
- `docs:` — Documentation
- `test:` — Tests
- `refactor:` — Refactoring without behavior change
- `perf:` — Performance improvement

### 4. Push & Open PR

```bash
git push origin feature/your-feature
```

Then open a PR on GitHub with:
- Clear title (same as commit message)
- Description of what and why
- Reference any related issues (#123)

## Testing Requirements

All PRs must:
- [ ] Pass `make test` (100%)
- [ ] Pass `make lint` (code style)
- [ ] Maintain or improve coverage
- [ ] Include new tests for new features
- [ ] Update docs if API changed

## Documentation

Update docs if you:
- Change public API
- Add new features
- Fix significant bugs
- Change architecture

## License

By contributing, you agree your code is under BSD 2-Clause License (see LICENSE).

## Questions?

- Check `GETTING_STARTED.md` for dev setup
- Check `ARCHITECTURE.md` for design questions
- Check `API.md` for protocol questions
- Open an issue for questions/discussions

---

Thank you for contributing! 🚀
