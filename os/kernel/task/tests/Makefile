############################################################################
#
# Copyright 2026 Samsung Electronics All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
############################################################################

.PHONY: check

TEST ?= all

check:
	@if [ "$(TEST)" != "all" ] && [ "$(TEST)" != "starthook_order" ]; then \
		echo "unknown task test: $(TEST)" >&2; \
		exit 2; \
	fi
	@sh ./test_starthook_order.sh
