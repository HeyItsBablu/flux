# scripts/spa_server.py
import os
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit

ROOT = sys.argv[1] if len(sys.argv) > 1 else "."


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def do_GET(self):
        # If the requested path doesn't correspond to a real file
        # (main.wasm, .js, .data, favicon, etc.), fall back to serving
        # flux_app.html and let the app's own Navigator figure out the
        # route from location.pathname. This is the standard "SPA
        # fallback" every real static host (Netlify, Vercel, nginx
        # try_files, etc.) provides — emrun's dev server does not.
        #
        # Strip query string / fragment before checking existence —
        # self.path is the raw request target (e.g. "/flux_app.wasm?v=2"),
        # and os.path.join'ing the query string onto the real filename
        # means a legitimate asset request with ANY query param would
        # never be found on disk and would incorrectly fall back to
        # serving flux_app.html instead of the actual file. Also unquote
        # so a %20-style encoded filename is checked correctly.
        route_path = unquote(urlsplit(self.path).path)
        requested = os.path.join(ROOT, route_path.lstrip("/"))

        if route_path == "/" or not os.path.exists(requested):
            self.path = "/flux_app.html"

        return super().do_GET()

    def handle_one_request(self):
        # Swallow the harmless ConnectionAbortedError/ConnectionResetError/
        # BrokenPipeError a request can throw mid-teardown during a Ctrl+C
        # shutdown race. This is the actual failure path (socket I/O
        # inside the request-handling loop) — a log_message() override
        # alone does NOT catch this, since the exception never reaches
        # logging at all.
        try:
            super().handle_one_request()
        except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
            pass

    def log_message(self, format, *args):
        super().log_message(format, *args)


def main():
    port = 6931
    # ThreadingHTTPServer instead of plain HTTPServer — a browser opens
    # several concurrent connections for flux_app.js/.wasm/.data/fonts;
    # the single-threaded base class serializes them and adds needless
    # latency to local dev.
    #
    # Bound to 127.0.0.1, not 0.0.0.0 — matches the printed "localhost"
    # URL below. If LAN access (e.g. testing hydration from a phone) is
    # ever needed, make that an explicit opt-in rather than the default.
    httpd = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"Serving {ROOT} with SPA fallback at http://localhost:{port}")
    print("Press Ctrl+C to stop.")

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[spa_server] Ctrl+C received, shutting down...")
    finally:
        httpd.server_close()
        print("[spa_server] Server closed.")


if __name__ == "__main__":
    main()