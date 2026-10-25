import { SECURITY_HEADERS } from "./security";

function redirectWithSecurity(request: Request): Response {
  const url = new URL(request.url);
  url.protocol = "https:";
  url.hostname = "radiostreamer.app";

  return new Response(null, {
    status: 301,
    headers: {
      Location: url.toString(),
      ...SECURITY_HEADERS,
    },
  });
}

export default {
  fetch(request: Request): Response {
    return redirectWithSecurity(request);
  },
};
