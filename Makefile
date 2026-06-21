CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -Iinclude
PHP_CONFIG ?= php-config
PHPIZE ?= phpize
PHP_BIN ?= /usr/bin/php8.3
MACHINE_SALT ?=
CRYPTO_SECRET ?=

BUILD_DIR := build
BIN_DIR := $(BUILD_DIR)/bin
OBJ_DIR := $(BUILD_DIR)/obj
BUILD_INCLUDE_DIR := $(BUILD_DIR)/include
BUILD_CONFIG_DIR := $(BUILD_INCLUDE_DIR)
PHP_EXT_DIR := $(BUILD_DIR)/php-ext
LOADER_OUT_DIR := $(BUILD_DIR)/loader

CONTAINER_OBJ := $(OBJ_DIR)/container.o
CRYPTO_OBJ := $(OBJ_DIR)/crypto.o
SHA256_OBJ := $(OBJ_DIR)/sha256.o
SEALBOX_OBJ := $(OBJ_DIR)/sealbox.o
ARGS_OBJ := $(OBJ_DIR)/args.o
RUNTIME_OBJ := $(OBJ_DIR)/runtime.o
ENV_OBJ := $(OBJ_DIR)/env.o
SUPERVISOR_OBJ := $(OBJ_DIR)/supervisor.o
MACHINE_CONFIG := $(BUILD_CONFIG_DIR)/machine.h

.PHONY: all native loader clean FORCE

all: native

native: $(BIN_DIR)/yapf-pack $(BIN_DIR)/yapf-seal $(BIN_DIR)/yapf-client $(BIN_DIR)/start

$(OBJ_DIR):
	mkdir -p "$(OBJ_DIR)"

$(BIN_DIR):
	mkdir -p "$(BIN_DIR)"

$(BUILD_CONFIG_DIR):
	mkdir -p "$(BUILD_CONFIG_DIR)"

FORCE:

$(MACHINE_CONFIG): FORCE | $(BUILD_CONFIG_DIR)
	@test -n "$(MACHINE_SALT)" || (echo "MACHINE_SALT is required" >&2; exit 1)
	@test -n "$(CRYPTO_SECRET)" || (echo "CRYPTO_SECRET is required" >&2; exit 1)
	@{ \
		echo '#ifndef YAPF_MACHINE_H'; \
		echo '#define YAPF_MACHINE_H'; \
		echo '#include <stddef.h>'; \
		echo '#define YAPF_CONFIG_MASK_BASE 137U'; \
		gen_array() { \
			name="$$1"; \
			value="$$2"; \
			printf 'static const unsigned char %s[] = {' "$$name"; \
			printf '%s' "$$value" | od -An -tu1 -v | awk 'BEGIN{idx=0; first=1} {for(i=1;i<=NF;i++){mask=(137+((idx*17)%251))%256; v=($$i+mask)%256; printf "%s%u", first?"":", ", v; first=0; idx++}} END{if(first) printf "0"}'; \
			printf '};\n'; \
			printf '#define %s_LEN ' "$$name"; \
			printf '%s' "$$value" | wc -c; \
		}; \
		gen_array YAPF_MACHINE_SALT_DATA "$(MACHINE_SALT)"; \
		gen_array YAPF_CRYPTO_SECRET_DATA "$(CRYPTO_SECRET)"; \
		echo 'static void yapf_unmask_config(const unsigned char *data, size_t len, unsigned char *out)'; \
		echo '{'; \
		echo '    for (size_t i = 0; i < len; i++) {'; \
		echo '        unsigned int mask = (YAPF_CONFIG_MASK_BASE + ((i * 17U) % 251U)) & 0xffU;'; \
		echo '        out[i] = (unsigned char)(((unsigned int)data[i] + 256U - mask) & 0xffU);'; \
		echo '    }'; \
		echo '}'; \
		echo '#endif'; \
	} > "$@"

$(CONTAINER_OBJ): src/core/container.c include/core/container.h include/crypto/crypto.h $(MACHINE_CONFIG) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_INCLUDE_DIR) -c src/core/container.c -o $@

$(CRYPTO_OBJ): src/crypto/crypto.c include/crypto/crypto.h include/core/container.h include/crypto/sealbox.h include/crypto/sha256.h $(MACHINE_CONFIG) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_INCLUDE_DIR) -c src/crypto/crypto.c -o $@

$(SHA256_OBJ): src/crypto/sha256.c include/crypto/sha256.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c src/crypto/sha256.c -o $@

$(SEALBOX_OBJ): src/crypto/sealbox.c include/crypto/sealbox.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c src/crypto/sealbox.c -o $@

$(ARGS_OBJ): src/cli/args.c include/cli/args.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c src/cli/args.c -o $@

$(RUNTIME_OBJ): src/runtime/runtime.c include/runtime/runtime.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c src/runtime/runtime.c -o $@

$(ENV_OBJ): src/runtime/env.c include/runtime/env.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c src/runtime/env.c -o $@

$(SUPERVISOR_OBJ): src/runtime/supervisor.c include/runtime/supervisor.h include/core/container.h $(MACHINE_CONFIG) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_INCLUDE_DIR) -c src/runtime/supervisor.c -o $@

$(BIN_DIR)/yapf-pack: src/cli/pack.c $(CONTAINER_OBJ) $(CRYPTO_OBJ) $(SHA256_OBJ) $(SEALBOX_OBJ) $(ARGS_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -D_XOPEN_SOURCE=700 -o $@ src/cli/pack.c $(CONTAINER_OBJ) $(CRYPTO_OBJ) $(SHA256_OBJ) $(SEALBOX_OBJ) $(ARGS_OBJ)

$(BIN_DIR)/yapf-seal: src/cli/seal.c $(CONTAINER_OBJ) $(CRYPTO_OBJ) $(SHA256_OBJ) $(SEALBOX_OBJ) $(ARGS_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ src/cli/seal.c $(CONTAINER_OBJ) $(CRYPTO_OBJ) $(SHA256_OBJ) $(SEALBOX_OBJ) $(ARGS_OBJ)

$(BIN_DIR)/yapf-client: src/cli/client.c $(CONTAINER_OBJ) $(CRYPTO_OBJ) $(SHA256_OBJ) $(SEALBOX_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ src/cli/client.c $(CONTAINER_OBJ) $(CRYPTO_OBJ) $(SHA256_OBJ) $(SEALBOX_OBJ)

$(BIN_DIR)/start: src/runtime/start.c $(RUNTIME_OBJ) $(ENV_OBJ) $(SUPERVISOR_OBJ) $(CONTAINER_OBJ) $(CRYPTO_OBJ) $(SHA256_OBJ) $(SEALBOX_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -DPHP_BIN=\"$(PHP_BIN)\" -o $@ src/runtime/start.c $(RUNTIME_OBJ) $(ENV_OBJ) $(SUPERVISOR_OBJ) $(CONTAINER_OBJ) $(CRYPTO_OBJ) $(SHA256_OBJ) $(SEALBOX_OBJ)

loader: $(MACHINE_CONFIG)
	rm -rf "$(PHP_EXT_DIR)" "$(LOADER_OUT_DIR)"
	mkdir -p "$(PHP_EXT_DIR)" "$(PHP_EXT_DIR)/include/core" "$(PHP_EXT_DIR)/include/crypto" "$(PHP_EXT_DIR)/include/runtime" "$(LOADER_OUT_DIR)"
	cp src/runtime/loader.c src/extension/config.m4 src/extension/license.c src/core/payload.c src/extension/stream.c src/core/container.c src/crypto/crypto.c src/crypto/sha256.c src/crypto/sealbox.c "$(PHP_EXT_DIR)/"
	cp include/core/container.h include/core/payload.h "$(PHP_EXT_DIR)/include/core/"
	cp include/crypto/crypto.h include/crypto/sealbox.h include/crypto/sha256.h "$(PHP_EXT_DIR)/include/crypto/"
	cp include/runtime/license.h include/runtime/loader.h include/runtime/stream.h "$(PHP_EXT_DIR)/include/runtime/"
	cp "$(MACHINE_CONFIG)" "$(PHP_EXT_DIR)/include/machine.h"
	cd "$(PHP_EXT_DIR)" && $(PHPIZE) && ./configure --with-php-config=$(PHP_CONFIG) --enable-yapf-loader && $(MAKE)
	cp "$(PHP_EXT_DIR)/modules/yapf_loader.so" "$(LOADER_OUT_DIR)/yapf_loader.so"

clean:
	rm -rf "$(BUILD_DIR)"