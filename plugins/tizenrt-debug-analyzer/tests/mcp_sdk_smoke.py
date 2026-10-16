"""Optional real MCP client test: run with mcp>=1.30,<2 installed.

The plugin runtime itself has no third-party dependency.
"""
import asyncio
import json
from pathlib import Path
import sys
import tempfile

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

ROOT = Path(__file__).resolve().parents[1]


async def main():
    with tempfile.TemporaryDirectory() as directory:
        log = Path(directory) / "uart.log"
        log.write_text("up_usagefault: Usage Fault: CFAULTS: 02000000\nAssertion failed at file:up_usagefault.c line: 100\n")
        params = StdioServerParameters(command=sys.executable,
            args=[str(ROOT / "server.py"), "--root", directory])
        async with stdio_client(params) as (read, write):
            async with ClientSession(read, write) as session:
                initialized = await session.initialize()
                listed = await session.list_tools()
                assert len(listed.tools) == 5
                result = await session.call_tool("analyze_log", {"path": str(log)})
                assert not result.isError
                report = json.loads(result.content[0].text)
                assert report["summary"]["incident_count"] == 1
                excerpts = await session.call_tool("get_log_excerpt", {"report_id": report["report_id"], "start_line": 1})
                assert "CFAULTS: 02000000" in excerpts.content[0].text
                catalog = await session.call_tool("list_log_formats", {})
                assert len(json.loads(catalog.content[0].text)["families"]) >= 30
                await session.send_ping()
                print(json.dumps({"status": "pass", "protocol": initialized.protocolVersion,
                    "tools": [tool.name for tool in listed.tools],
                    "checks": ["initialize", "tools/list", "analyze file", "evidence excerpt", "catalog", "ping"]}))


if __name__ == "__main__":
    asyncio.run(main())
