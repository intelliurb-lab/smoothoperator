.PHONY: all debug release test coverage lint clean setup install install-systemd watch help

# Build directories
BUILD_DIR := build
SOURCE_DIR := src
TEST_DIR := test
INCLUDE_DIR := include

# Installation directories (customize with: make install BASE_DIR=/opt/radio)
BASE_DIR ?= /usr/local
BIN_DIR ?= $(BASE_DIR)/bin
CONFIG_DIR ?= /etc/smoothoperator
LOG_DIR ?= /var/log/smoothoperator
CONFIG_FILE ?= $(CONFIG_DIR)/smoothoperator.env

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
	@echo "✓ Binário em ./$(BUILD_DIR)/smoothoperator"

release: setup
	@echo "==> Build RELEASE..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release ..
	@cd $(BUILD_DIR) && make -j$$(nproc)
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
	@echo "==> Installation Settings:"
	@echo "    BASE_DIR:     $(BASE_DIR)"
	@echo "    BIN_DIR:      $(BIN_DIR)"
	@echo "    CONFIG_DIR:   $(CONFIG_DIR)"
	@echo "    LOG_DIR:      $(LOG_DIR)"
	@echo ""
	@echo "==> Binary will be installed to $(BIN_DIR)/smoothoperator"
	@sha256sum $(BUILD_DIR)/smoothoperator
	@read -p "Proceed? [y/N] " confirm && [ "$$confirm" = "y" ] || exit 1
	@mkdir -p $(BIN_DIR) $(CONFIG_DIR) $(LOG_DIR)
	@install -D -m 755 $(BUILD_DIR)/smoothoperator $(BIN_DIR)/smoothoperator
	@install -D -m 640 smoothoperator.env.example $(CONFIG_FILE).example
	@if [ ! -f $(CONFIG_FILE) ]; then \
		cp $(CONFIG_FILE).example $(CONFIG_FILE); \
		chmod 640 $(CONFIG_FILE); \
		echo "✓ Config template copied to $(CONFIG_FILE)"; \
	else \
		echo "⚠ Config file exists, template saved as $(CONFIG_FILE).example"; \
	fi
	@echo "✓ Instalado em $(BIN_DIR)/smoothoperator"
	@echo "✓ Config em $(CONFIG_FILE) (edit before starting)"
	@echo "✓ Log dir: $(LOG_DIR)"

install-systemd: install
	@echo ""
	@echo "==> Criando systemd service..."
	@mkdir -p scripts
	@echo '[Unit]' > scripts/smoothoperator.service.tmp
	@echo 'Description=SmoothOperator - RabbitMQ to Liquidsoap Controller' >> scripts/smoothoperator.service.tmp
	@echo 'After=network.target rabbitmq-server.service' >> scripts/smoothoperator.service.tmp
	@echo 'Wants=rabbitmq-server.service' >> scripts/smoothoperator.service.tmp
	@echo '' >> scripts/smoothoperator.service.tmp
	@echo '[Service]' >> scripts/smoothoperator.service.tmp
	@echo 'Type=simple' >> scripts/smoothoperator.service.tmp
	@echo 'User=root' >> scripts/smoothoperator.service.tmp
	@echo 'Group=root' >> scripts/smoothoperator.service.tmp
	@echo 'WorkingDirectory=$(BASE_DIR)' >> scripts/smoothoperator.service.tmp
	@echo 'EnvironmentFile=$(CONFIG_FILE)' >> scripts/smoothoperator.service.tmp
	@echo 'ExecStart=$(BIN_DIR)/smoothoperator' >> scripts/smoothoperator.service.tmp
	@echo 'Restart=on-failure' >> scripts/smoothoperator.service.tmp
	@echo 'RestartSec=10' >> scripts/smoothoperator.service.tmp
	@echo 'StandardOutput=journal' >> scripts/smoothoperator.service.tmp
	@echo 'StandardError=journal' >> scripts/smoothoperator.service.tmp
	@echo 'SyslogIdentifier=smoothoperator' >> scripts/smoothoperator.service.tmp
	@echo '' >> scripts/smoothoperator.service.tmp
	@echo '[Install]' >> scripts/smoothoperator.service.tmp
	@echo 'WantedBy=multi-user.target' >> scripts/smoothoperator.service.tmp
	@install -D -m 644 scripts/smoothoperator.service.tmp /etc/systemd/system/smoothoperator.service
	@rm -f scripts/smoothoperator.service.tmp
	@systemctl daemon-reload
	@echo "✓ Systemd service criado em /etc/systemd/system/smoothoperator.service"
	@echo ""
	@echo "==> Próximos passos:"
	@echo "    1. Edit config: nano $(CONFIG_FILE)"
	@echo "    2. Enable service: systemctl enable smoothoperator"
	@echo "    3. Start service: systemctl start smoothoperator"
	@echo "    4. Check status: systemctl status smoothoperator"
	@echo "    5. View logs: journalctl -u smoothoperator -f"

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
