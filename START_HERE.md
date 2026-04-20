# 🚀 Memphis — Start Here

Welcome! This is the directory structure for Memphis, the new Intelliurb FM RabbitMQ controller in C.

## 📍 You Are Here

```
~/src/ls-controller/  (or /opt/radio/memphis/)
```

## 📚 Documentation (Pick Your Level)

### 🏃 **I'm in a hurry** (5 min read)
→ **[QUICK_REFERENCE.md](QUICK_REFERENCE.md)** — Commands, checklist, cheat sheet

### 🚀 **I want to build it** (15 min read)
→ **[GETTING_STARTED.md](GETTING_STARTED.md)** — Setup, dev workflow, roadmap

### 🎯 **I want to understand it** (30 min read)
→ **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** — Design, components, data flow

### 📡 **I need API details** (10 min read)
→ **[API.md](docs/API.md)** — RabbitMQ messages, examples, protocol

### 📊 **I want the big picture** (10 min read)
→ **[SUMMARY.md](SUMMARY.md)** — What was delivered, what's next

### ❓ **General overview**
→ **[README.md](README.md)** — Features, quick start, status

---

## 🎯 Quick Start (3 steps)

### Step 1: Install Dependencies
```bash
sudo apt-get install -y build-essential cmake librabbitmq-dev libjansson-dev criterion-dev
```

### Step 2: Build
```bash
cd ~/src/ls-controller
make debug
```

### Step 3: Test
```bash
make test
```

✅ If all pass, you're ready to develop!

---

## 🛣️ Development Path

```
Phase 1-2: Structure & Skeleton ✅ DONE
    ↓
Phase 1.1: TCP Socket Persistent (1-2 days)
    ↓
Phase 1.2: RabbitMQ Consumer (2-3 days)
    ↓
Phase 1.3: Event Routing (2-3 days)
    ↓
Phase 1.4: E2E Integration (2-3 days)
    ↓
v1.0 Complete! 🎉
```

See `GETTING_STARTED.md` Section 8 for detailed roadmap.

---

## 📁 Project Structure

```
.
├── src/              ← C source code (7 files)
├── include/          ← Headers (6 files)
├── test/             ← Critério tests (4 files)
├── docs/
│   ├── ARCHITECTURE.md
│   ├── API.md
│   └── ADR/
├── scripts/          ← RabbitMQ setup
├── CMakeLists.txt    ← CMake build config
├── Makefile          ← Build targets
├── .clang-format     ← Code style
├── .gitignore        ← Git exclusions
└── *.md              ← Documentation
```

---

## 🔧 Build Targets

| Command | What | When |
|---------|------|------|
| `make debug` | Build with symbols | Development |
| `make release` | Build optimized | Production |
| `make test` | Run tests | Before commit |
| `make coverage` | Coverage report | Code quality |
| `make format` | Auto-format code | Before commit |
| `make lint` | Check formatting | Before commit |
| `make clean` | Remove build/ | Cleanup |

## ✅ Before You Commit

```bash
make format    # Auto-format
make lint      # Check style
make test      # Run tests
make coverage  # Check coverage
```

Then:
```bash
git add src/ test/ include/
git commit -m "feat: description of change"
```

---

## 📞 Need Help?

| Question | Answer |
|----------|--------|
| How do I build? | → `make debug` or `GETTING_STARTED.md` Step 3 |
| How do tests work? | → `GETTING_STARTED.md` Section 7 |
| What's the architecture? | → `ARCHITECTURE.md` |
| What messages are sent? | → `API.md` |
| What's the roadmap? | → `GETTING_STARTED.md` Section 8 |
| What's the quick reference? | → `QUICK_REFERENCE.md` |

---

## 🎓 Learning Path

**New to the project?**
1. Read `README.md` (overview)
2. Read `QUICK_REFERENCE.md` (commands)
3. Read `GETTING_STARTED.md` (setup + roadmap)
4. Run `make debug test` (verify it works)

**Ready to code?**
1. Pick a task from `GETTING_STARTED.md` Section 8
2. Write tests in `test/`
3. Implement in `src/`
4. Run `make format test` (verify)
5. Commit

**Want deep dive?**
1. Read `ARCHITECTURE.md` (design)
2. Read `API.md` (messages)
3. Explore `src/` (stubs ready for implementation)

---

## 🚀 Next Steps

### 👉 **If you're starting now:**
Read `GETTING_STARTED.md` and follow Section 3 (Build) and 4 (Tests).

### 👉 **If you want to code:**
Pick Phase 1.1 from `GETTING_STARTED.md` Section 8, and start in `src/liquidsoap_client.c`.

### 👉 **If you have questions:**
Check the relevant `.md` file, then ask.

---

## 📊 Project Status

- ✅ Documentation: Complete
- ✅ Build system: Ready
- ✅ Code skeleton: Ready (stubs)
- ✅ Tests (framework): Ready
- 🟡 Implementation: Phase 1.1 (TCP Socket) starting next

**Last updated**: 2026-04-20

---

**You're all set!** Pick a guide above and start. 🎯
