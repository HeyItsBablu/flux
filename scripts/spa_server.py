# scripts/spa_server.py
import http.server, os, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "."

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def do_GET(self):
        # If the requested path doesn't correspond to a real file
        # (main.wasm, .js, .data, favicon, etc.), fall back to serving
        # flux_app.html and let the app's own Navigator figure out the
        # route from location.pathname. This is the standard "SPA
        # fallback" every real static host (Netlify, Vercel, nginx
        # try_files, etc.) provides — emrun's dev server does not.
        requested = os.path.join(ROOT, self.path.lstrip("/"))
        if self.path != "/" and not os.path.exists(requested):
            self.path = "/flux_app.html"
        elif self.path == "/":
            self.path = "/flux_app.html"
        return super().do_GET()

if __name__ == "__main__":
    port = 6931
    with http.server.HTTPServer(("0.0.0.0", port), Handler) as httpd:
        print(f"Serving {ROOT} with SPA fallback at http://localhost:{port}")
        httpd.serve_forever()