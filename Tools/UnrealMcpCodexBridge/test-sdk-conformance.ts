import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";

const mcpUrl = process.env.UEVOLVE_MCP_URL ?? "http://127.0.0.1:8765/mcp";
const timeoutMs = 10_000;
const rawFetchTimeoutMs = 2_000;

type Step = {
  name: string;
  run: () => Promise<void>;
};

let passed = 0;
let failed = 0;

function assert(condition: unknown, message: string): asserts condition {
  if (!condition) {
    throw new Error(message);
  }
}

function isEndpointUnreachable(error: unknown): boolean {
  const text = error instanceof Error ? `${error.name}: ${error.message}` : String(error);
  return /ECONNREFUSED|fetch failed|connection refused|Failed to fetch|NetworkError|Unable to connect/i.test(text);
}

function formatError(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

async function withTimeout<T>(promise: Promise<T>, message: string): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  const timeout = new Promise<never>((_, reject) => {
    timer = setTimeout(() => reject(new Error(message)), timeoutMs);
  });

  try {
    return await Promise.race([promise, timeout]);
  } finally {
    if (timer) {
      clearTimeout(timer);
    }
  }
}

async function rawFetch(pathUrl: string, init: RequestInit): Promise<Response> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), rawFetchTimeoutMs);
  try {
    return await fetch(pathUrl, { ...init, signal: controller.signal });
  } finally {
    clearTimeout(timer);
  }
}

async function main(): Promise<void> {
  const client = new Client({ name: "ueatelier-sdk-conformance", version: "0.1.0" });
  let connected = false;

  const steps: Step[] = [
    {
      name: "initialize",
      run: async () => {
        const transport = new StreamableHTTPClientTransport(new URL(mcpUrl));
        await client.connect(transport);
        connected = true;

        const serverInfo = client.getServerVersion();
        assert(serverInfo?.name === "unreal-editor-mcp", `expected serverInfo.name unreal-editor-mcp, got ${serverInfo?.name ?? "<missing>"}`);
      },
    },
    {
      name: "listTools",
      run: async () => {
        const result = await client.listTools();
        assert(result.tools.length > 170, `expected >170 tools, got ${result.tools.length}`);
        assert(result.tools.some((tool) => tool.name === "unreal.editor_status"), "unreal.editor_status missing from tools/list");
      },
    },
    {
      name: "callTool unreal.editor_status",
      run: async () => {
        const result = await client.callTool({ name: "unreal.editor_status", arguments: {} });
        assert(result.isError !== true, "unreal.editor_status returned isError=true");
        assert(Array.isArray(result.content) && result.content.length > 0, "unreal.editor_status returned empty content");
      },
    },
    {
      name: "raw GET /mcp",
      run: async () => {
        const response = await rawFetch(mcpUrl, { method: "GET", headers: { Accept: "text/event-stream" } });
        assert(response.status === 405, `expected HTTP 405, got ${response.status}`);
      },
    },
    {
      name: "raw DELETE /mcp",
      run: async () => {
        const response = await rawFetch(mcpUrl, { method: "DELETE" });
        assert(response.status === 405, `expected HTTP 405, got ${response.status}`);
      },
    },
    {
      name: "raw initialized notification",
      run: async () => {
        const response = await rawFetch(mcpUrl, {
          method: "POST",
          headers: { Accept: "application/json, text/event-stream", "Content-Type": "application/json" },
          body: JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }),
        });
        const body = await response.text();
        assert(response.status === 202, `expected HTTP 202, got ${response.status}`);
        assert(body.length === 0, `expected empty body, got ${body.length} bytes`);
      },
    },
  ];

  try {
    await withTimeout(
      (async () => {
        for (const step of steps) {
          try {
            await step.run();
            passed += 1;
            console.log(`PASS ${step.name}`);
          } catch (error) {
            failed += 1;
            console.error(`FAIL ${step.name}: ${formatError(error)}`);
            if (step.name === "initialize" && isEndpointUnreachable(error)) {
              console.error(`Endpoint unreachable at ${mcpUrl}; is the Unreal Editor with UnrealMcp running?`);
            }
            throw error;
          }
        }
      })(),
      `Timed out after ${timeoutMs}ms; is the Unreal Editor with UnrealMcp running?`,
    );
  } finally {
    if (connected) {
      await client.close();
    }
  }

  console.log(`SDK conformance summary: ${passed} passed, ${failed} failed`);
}

main().catch(() => {
  console.error(`SDK conformance summary: ${passed} passed, ${failed || 1} failed`);
  process.exit(1);
});
