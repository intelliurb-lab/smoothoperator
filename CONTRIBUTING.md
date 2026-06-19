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

## Error Handling Convention

Follow this pattern for consistency across modules:

- **Core/Business Logic:** Use `std::variant<T, std::error_code>` for recoverable errors
- **Configuration/Startup:** Throw `std::runtime_error` with descriptive messages
- **Event Loop Callbacks:** Return void; log errors with `std::cerr`
- **Input Validation:** Throw `std::invalid_argument` for malformed user input

Example:
```cpp
// Good: Core module with variant result
core::Result<std::string> execute(const std::string& cmd) {
    if (cmd.empty()) return std::make_error_code(std::errc::invalid_argument);
    // ...
}

// Good: Config startup with exceptions
if (!config_path.exists()) {
    throw std::runtime_error("Config file not found: " + config_path);
}
```

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
