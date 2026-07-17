.PHONY: generate clean-generated watcher test

GENERATED_DIR := generated
GENERATED_PATTERNS := bpf_*.go bpf_*.o

watcher: ## Build the watcher binary
	cd ./cmd/watcher && CGO_ENABLED=0 go build -mod=vendor -o ../../$@ .
	strip $@

test: ## Run the unit tests
	go test --mod=vendor ./...

generate:
	@mkdir -p $(GENERATED_DIR)
	go generate ./...
	@for f in $(GENERATED_PATTERNS); do \
		if [ -L "$$f" ]; then \
			rm -f "$$f"; \
			if [ -f "$(GENERATED_DIR)/$$f" ]; then \
				cp -f "$(GENERATED_DIR)/$$f" "$$f"; \
			elif [ -f "$(GENERATED_DIR)/$$f.gen" ]; then \
				cp -f "$(GENERATED_DIR)/$$f.gen" "$$f"; \
			fi; \
		fi; \
		if [ -f "$$f" ]; then \
			case "$$f" in \
				*.go) cp -f "$$f" "$(GENERATED_DIR)/$$f.gen" ;; \
				*.o) cp -f "$$f" "$(GENERATED_DIR)/$$f" ;; \
			esac; \
		fi; \
	done

clean-generated:
	@rm -rf "$(GENERATED_DIR)"
