.PHONY: all debug release test coverage lint clean setup install

# Diretórios
BUILD_DIR := build
SOURCE_DIR := src
TEST_DIR := test
INCLUDE_DIR := include

# Configurações
CMAKE := cmake
CMAKE_FLAGS := -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Targets
all: debug

# ============================================================================
# Build
# ============================================================================

setup:
	@echo "==> Instalando dependências..."
	@command -v pkg-config >/dev/null 2>&1 || (echo "Instale: pkg-config"; exit 1)
	@pkg-config --cflags --libs librabbitmq >/dev/null 2>&1 || \
		(echo "Instale: librabbitmq-dev (apt: librabbitmq-dev ou brew: librabbitmq)"; exit 1)
	@pkg-config --cflags --libs jansson >/dev/null 2>&1 || \
		(echo "Instale: jansson (apt: libjansson-dev ou brew: jansson)"; exit 1)
	@pkg-config --cflags --libs cunit >/dev/null 2>&1 || \
		(echo "Instale: cunit (apt: libcunit1-dev ou brew: cunit)"; exit 1)
	@echo "✓ Dependências OK"

debug: setup
	@echo "==> Build DEBUG..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Debug ..
	@cd $(BUILD_DIR) && make -j$$(nproc)
	@echo "✓ Binário em ./$(BUILD_DIR)/bin/memphis"

release: setup
	@echo "==> Build RELEASE..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release ..
	@cd $(BUILD_DIR) && make -j$$(nproc)
	@echo "✓ Binário em ./$(BUILD_DIR)/bin/memphis"

# ============================================================================
# Testes
# ============================================================================

test: debug
	@echo "==> Rodando testes..."
	@cd $(BUILD_DIR) && ctest --output-on-failure --verbose
	@echo "✓ Testes OK"

coverage: debug
	@echo "==> Gerando coverage..."
	@cd $(BUILD_DIR) && make
	@lcov --capture --directory . --output-file coverage.info >/dev/null 2>&1 || \
		(echo "Instale lcov: apt install lcov"; exit 1)
	@lcov --remove coverage.info '*/test/*' '/usr/*' --output-file coverage.info
	@genhtml coverage.info --output-directory coverage_html
	@echo "✓ Coverage em ./$(BUILD_DIR)/coverage_html/index.html"

# ============================================================================
# Qualidade de Código
# ============================================================================

lint:
	@echo "==> Lint (clang-format check)..."
	@command -v clang-format >/dev/null 2>&1 || \
		(echo "Instale: clang-format (apt: clang-format ou brew: clang-format)"; exit 1)
	@for f in $(SOURCE_DIR)/*.c $(INCLUDE_DIR)/*.h $(TEST_DIR)/*.c; do \
		if [ -f "$$f" ]; then \
			clang-format --dry-run --Werror "$$f" || exit 1; \
		fi; \
	done
	@echo "✓ Lint OK"

format:
	@echo "==> Reformatando código..."
	@clang-format -i $(SOURCE_DIR)/*.c $(INCLUDE_DIR)/*.h $(TEST_DIR)/*.c
	@echo "✓ Código formatado"

static-analysis:
	@echo "==> Static analysis (cppcheck)..."
	@command -v cppcheck >/dev/null 2>&1 || \
		(echo "Instale: cppcheck (apt: cppcheck ou brew: cppcheck)"; exit 1)
	@cppcheck --std=c11 --enable=all --suppress=missingIncludeSystem \
		$(SOURCE_DIR) $(INCLUDE_DIR)
	@echo "✓ Static analysis OK"

# ============================================================================
# Limpeza
# ============================================================================

clean:
	@echo "==> Limpando..."
	@rm -rf $(BUILD_DIR) compile_commands.json
	@echo "✓ Limpo"

distclean: clean
	@echo "==> Limpeza profunda..."
	@find . -name "*.o" -delete
	@find . -name "*.a" -delete
	@find . -name "*~" -delete
	@echo "✓ Limpo completamente"

# ============================================================================
# Instalação
# ============================================================================

install: release
	@echo "==> Binary will be installed to /usr/local/bin/memphis"
	@sha256sum $(BUILD_DIR)/bin/memphis
	@read -p "Proceed? [y/N] " confirm && [ "$$confirm" = "y" ] || exit 1
	@sudo install -D -m 755 $(BUILD_DIR)/bin/memphis /usr/local/bin/memphis
	@echo "✓ Instalado em /usr/local/bin/memphis"

install-systemd:
	@echo "==> Instalando systemd service..."
	@sudo install -D -m 644 scripts/memphis.service /etc/systemd/system/
	@sudo install -D -m 640 memphis.env /etc/memphis/memphis.env
	@sudo systemctl daemon-reload
	@echo "✓ Service instalado. Execute: sudo systemctl start memphis"

# ============================================================================
# Development
# ============================================================================

watch:
	@echo "==> Watching para mudanças..."
	@while true; do \
		clear; \
		$(MAKE) test 2>&1 | head -50; \
		inotifywait -e modify -r $(SOURCE_DIR) $(TEST_DIR) $(INCLUDE_DIR); \
	done

# ============================================================================
# Ajuda
# ============================================================================

help:
	@echo "Memphis — Intelliurb FM Controller"
	@echo ""
	@echo "Targets:"
	@echo "  make debug              Build com símbolos (default)"
	@echo "  make release            Build otimizado"
	@echo "  make test               Rodar testes"
	@echo "  make coverage           Coverage report"
	@echo "  make lint               Check formatting"
	@echo "  make format             Reformat código"
	@echo "  make static-analysis    cppcheck"
	@echo "  make install            Install /usr/local/bin/memphis"
	@echo "  make install-systemd    Install systemd service"
	@echo "  make watch              Watch + test contínuo"
	@echo "  make clean              Remove build/"
	@echo "  make distclean          Remove tudo"
	@echo ""
