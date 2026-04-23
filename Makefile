.PHONY: all debug release test coverage lint clean setup install install-systemd watch help

# Build directories
BUILD_DIR := build
SOURCE_DIR ?= .
INCLUDE_DIR := include

# Installation directories
BASE_DIR ?= /usr/local
CMAKE_INSTALL_PREFIX := $(BASE_DIR)
CONFIG_DIR ?= $(CMAKE_INSTALL_PREFIX)/etc/smoothoperator
LOG_DIR ?= /var/log/smoothoperator

# Configurações
CMAKE := cmake
CMAKE_FLAGS := -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
               -DCMAKE_INSTALL_PREFIX=$(CMAKE_INSTALL_PREFIX) \
               -DSMOOTHOP_CONFIG_DIR=$(CONFIG_DIR) \
               -DSMOOTHOP_LOG_DIR=$(LOG_DIR)

# Targets
all: debug

# ============================================================================
# Build
# ============================================================================

setup:
	@echo "==> Verificando dependências de sistema..."
	@command -v $(CMAKE) >/dev/null 2>&1 || (echo "Erro: cmake não encontrado."; exit 1)
	@ls /usr/include/ev.h >/dev/null 2>&1 || ls /usr/local/include/ev.h >/dev/null 2>&1 || (echo "Erro: libev-dev não encontrado (apt install libev-dev)"; exit 1)
	@pkg-config --exists openssl || (echo "Erro: libssl-dev não encontrado (apt install libssl-dev)"; exit 1)
	@echo "✓ Dependências de sistema OK"

debug: setup
	@echo "==> Configurando build DEBUG em $(BUILD_DIR)..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Debug ..
	@echo "==> Compilando..."
	@$(CMAKE) --build $(BUILD_DIR) -j$$(nproc)
	@echo "✓ Binário em ./$(BUILD_DIR)/smoothoperator"

release: setup
	@echo "==> Configurando build RELEASE em $(BUILD_DIR)..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release ..
	@echo "==> Compilando..."
	@$(CMAKE) --build $(BUILD_DIR) -j$$(nproc)
	@echo "✓ Binário em ./$(BUILD_DIR)/smoothoperator"

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
	@echo "==> Instalando em $(CMAKE_INSTALL_PREFIX)..."
	@sudo $(CMAKE) --install $(BUILD_DIR)
	@echo "✓ Instalação concluída."

install-systemd: install
	@echo "==> Configurando systemd..."
	@scripts/setup-service.sh $(CMAKE_INSTALL_PREFIX)

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
	@echo "SmoothOperator — RabbitMQ to Liquidsoap Controller"
	@echo ""
	@echo "Targets:"
	@echo "  make debug              Build com símbolos (default)"
	@echo "  make release            Build otimizado"
	@echo "  make test               Rodar testes"
	@echo "  make coverage           Coverage report"
	@echo "  make lint               Check formatting"
	@echo "  make format             Reformat código"
	@echo "  make static-analysis    cppcheck"
	@echo "  make install            Build + install binary + config"
	@echo "  make install-systemd    Install systemd service (calls install)"
	@echo "  make watch              Watch + test contínuo"
	@echo "  make clean              Remove build/"
	@echo "  make distclean          Remove tudo"
	@echo ""
	@echo "Installation variables (customize with: make install BASE_DIR=...)"
	@echo "  BASE_DIR                Base installation path (default: /usr/local)"
	@echo "  BIN_DIR                 Binary path (default: BASE_DIR/bin)"
	@echo "  CONFIG_DIR              Config path (default: /etc/smoothoperator)"
	@echo "  LOG_DIR                 Log path (default: /var/log/smoothoperator)"
	@echo ""
	@echo "Examples:"
	@echo "  make install                                    # Default /usr/local/bin"
	@echo "  make install BASE_DIR=/opt/radio               # Custom path"
	@echo "  make install BASE_DIR=/opt/radio CONFIG_DIR=/etc/radio"
	@echo "  make install-systemd BASE_DIR=/opt/radio       # With systemd"
	@echo ""
