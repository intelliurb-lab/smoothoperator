# Contributing to SmoothOperator

Thank you for your interest in contributing!

## Getting Started

```bash
git clone https://github.com/intelliurb-lab/smoothoperator.git
cd smoothoperator

# Ubuntu/Debian
sudo apt-get install -y build-essential cmake pkg-config \
  librabbitmq-dev libjansson-dev libcunit1-dev

make debug
make test
```

## Workflow

1. Create a branch: `git checkout -b feature/your-feature`
2. Make changes and test: `make test`
3. Commit with clear messages: `git commit -m "feat: description"`
4. Push and open a Pull Request

## Code Style

- Use `const` by default
- Check return values
- Free all allocations
- Use safe string functions
- Validate input

## Testing

```bash
make test       # Run unit tests
make debug      # Build with ASAN/UBSAN
make format     # Auto-format code
make lint       # Check style
```

## Security

For security issues, email contact@intelliurb.com instead of opening a GitHub issue.

---

**Thank you for contributing!** 🚀
