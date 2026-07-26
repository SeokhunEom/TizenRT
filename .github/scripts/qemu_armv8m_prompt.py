from __future__ import annotations

import re
from typing import Final


PROMPT_RE: Final = re.compile(rb"TASH>>")
NOT_REGISTERED_RE: Final = re.compile(rb"TASH: cmd \(kernel_tc\) not registered")
EVENT_TAIL_BYTES: Final = max(len(PROMPT_RE.pattern), len(NOT_REGISTERED_RE.pattern)) - 1


class PromptEpoch:
    __slots__ = ("event_tail", "prompt_epoch", "retry_prompt_epoch", "need_command")

    def __init__(self) -> None:
        self.event_tail = bytearray()
        self.prompt_epoch = 0
        self.retry_prompt_epoch = 0
        self.need_command = True

    def consumes_fresh_prompt(self, chunk: bytes) -> bool:
        tail_length = len(self.event_tail)
        serial = bytes(self.event_tail) + chunk
        events = [
            (match.start(), False)
            for match in NOT_REGISTERED_RE.finditer(serial)
            if match.end() > tail_length
        ]
        events.extend(
            (match.start(), True)
            for match in PROMPT_RE.finditer(serial)
            if match.end() > tail_length
        )
        events.sort()
        self.event_tail[:] = serial[-EVENT_TAIL_BYTES:]
        for _, is_prompt in events:
            if not is_prompt:
                self.need_command = True
                self.retry_prompt_epoch = self.prompt_epoch
                continue
            self.prompt_epoch += 1
            if self.need_command and self.prompt_epoch > self.retry_prompt_epoch:
                self.need_command = False
                return True
        return False
