import { setSecurityHeaders } from "./security";

interface Env {
  ASSETS: { fetch: (request: Request) => Promise<Response> };
  DOWNLOADS: R2Bucket;
}

const DOWNLOAD_PREFIX = "/download/";
const LATEST_KEY = "latest.json";

// Normalized download names resolved through latest.json, so release uploads
// only need the versioned artifact plus an updated manifest.
const LATEST_ALIASES = new Set(["macos.zip"]);

const MUTABLE_CACHE_CONTROL = "public, max-age=300, s-maxage=300, stale-while-revalidate=86400";
const IMMUTABLE_CACHE_CONTROL = "public, max-age=31536000, immutable";

function redirectToHttps(request: Request): Response | null {
  const url = new URL(request.url);
  const forwardedProto = request.headers.get("x-forwarded-proto")?.toLowerCase();
  const cfVisitor = request.headers.get("cf-visitor")?.toLowerCase() ?? "";
  const isHttp =
    url.protocol === "http:" ||
    forwardedProto === "http" ||
    cfVisitor.includes('"scheme":"http"');

  if (!isHttp) return null;

  url.protocol = "https:";

  const headers = new Headers({ Location: url.toString() });
  setSecurityHeaders(headers);

  return new Response(null, { status: 301, headers });
}

async function latestReleaseName(env: Env): Promise<string | null> {
  const manifest = await env.DOWNLOADS.get(LATEST_KEY);
  if (!manifest) return null;

  try {
    const { name } = (await manifest.json()) as { name?: unknown };
    return typeof name === "string" && name.length > 0 ? name : null;
  } catch {
    return null;
  }
}

async function serveDownload(request: Request, env: Env): Promise<Response | null> {
  const url = new URL(request.url);
  if (!url.pathname.startsWith(DOWNLOAD_PREFIX)) return null;

  if (request.method !== "GET" && request.method !== "HEAD") {
    return new Response("Method Not Allowed", {
      status: 405,
      headers: { Allow: "GET, HEAD" },
    });
  }

  let key = decodeURIComponent(url.pathname.slice(DOWNLOAD_PREFIX.length));
  let isAlias = false;
  if (LATEST_ALIASES.has(key)) {
    const name = await latestReleaseName(env);
    if (!name) return null;
    key = name;
    isAlias = true;
  }

  const object = await env.DOWNLOADS.get(key);
  if (!object) return null;

  const headers = new Headers();
  setSecurityHeaders(headers);
  headers.set("Content-Type", object.httpMetadata?.contentType ?? "application/octet-stream");
  headers.set("Content-Length", String(object.size));
  headers.set("ETag", object.httpEtag);

  if (key === LATEST_KEY) {
    headers.set("Cache-Control", MUTABLE_CACHE_CONTROL);
  } else {
    headers.set("Content-Disposition", `attachment; filename="${key}"`);
    headers.set("Cache-Control", isAlias ? MUTABLE_CACHE_CONTROL : IMMUTABLE_CACHE_CONTROL);
  }

  if (request.method === "HEAD") {
    return new Response(null, { status: 200, headers });
  }

  return new Response(object.body, { status: 200, headers });
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const redirect = redirectToHttps(request);
    if (redirect) return redirect;

    const download = await serveDownload(request, env);
    if (download) return download;

    const response = await env.ASSETS.fetch(request);
    const headers = new Headers(response.headers);
    setSecurityHeaders(headers);

    return new Response(response.body, {
      status: response.status,
      statusText: response.statusText,
      headers,
    });
  },
};
