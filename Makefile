CC := clang-19
LINTER := clang-tidy-19
FORMATTER := clang-format-19

TARGET := daft-container
CFLAGS += -std=c23 -D_GNU_SOURCE -Wall -Wextra -pedantic
ROOTFS := rootfs

all: $(TARGET)

lint:
	@$(LINTER) --config-file=.clang-tidy --quiet *.c -- $(CFLAGS)

fmt:
	@$(FORMATTER) -style=file -i *.c

setup_apt:
	@apt update
	@apt install -y $(CC) $(LINTER) $(FORMATTER) clang-tools \
		debian-archive-keyring mmdebstrap

rootfs:
	@rm -fr "${ROOTFS}"
	@mkdir -p "${ROOTFS}"
	@mmdebstrap \
		--variant=important \
		--skip=output/mknod \
		stable > "${ROOTFS}".tar
	@tar xf "${ROOTFS}.tar" -C "${ROOTFS}"
	@rm "${ROOTFS}.tar"

clean:
	@rm -f $(TARGET)

.PHONY: lint fmt clean rootfs setup_apt
