CC := clang-19
LINTER := clang-tidy-19
FORMATTER := clang-format-19

TARGET := daft-container
CFLAGS += -std=c23 -D_GNU_SOURCE -Wall -Wextra -pedantic

all: $(TARGET)

lint:
	@$(LINTER) --config-file=.clang-tidy --quiet *.c -- $(CFLAGS)

format:
	@$(FORMATTER) -style=file -i *.c

clean:
	@rm -f $(TARGET)

.PHONY: lint format clean
