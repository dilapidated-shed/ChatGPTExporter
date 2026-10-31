// Adapted from GrokExporter commit 85922d6; provider behavior is intentionally absent here.
import { cp, mkdir, rm } from "node:fs/promises";
import path from "node:path";
import { build } from "esbuild";

const root = process.cwd();
const output = path.join(root, "dist", "extension");
await rm(output, { recursive: true, force: true });
await mkdir(output, { recursive: true });

const entries = {
  "service-worker": "src/extension/service-worker.ts",
  "page-bridge": "src/extension/page-bridge.ts",
  "content-relay": "src/extension/content-relay.ts",
  dashboard: "src/extension/dashboard.ts",
};

await Promise.all(Object.entries(entries).map(([name, entry]) => build({
  entryPoints: [path.join(root, entry)],
  outfile: path.join(output, `${name}.js`),
  bundle: true,
  format: "iife",
  platform: "browser",
  target: ["chrome120"],
  sourcemap: false,
  minify: false,
  legalComments: "inline",
})));

await cp(path.join(root, "public"), output, { recursive: true });

// The offline release uses only Node built-ins at runtime. Keep esbuild and
// TypeScript on the build host rather than requiring them beside private data.
const offline = path.join(root, "dist", "offline", "scripts");
await mkdir(offline, { recursive: true });
await build({
  entryPoints: [path.join(root, "src/chatgpt/offline-source.ts")],
  outfile: path.join(offline, "offline-core.mjs"), bundle: true,
  format: "esm", platform: "node", target: ["node22"], legalComments: "none",
});
for (const name of ["import-offline.mjs", "offline-stream.mjs", "verify-offline.mjs"])
  await cp(path.join(root, "scripts", name), path.join(offline, name));
