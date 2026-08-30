using System.Net;

namespace OracleShell;

/// <summary>
/// Minimal static file server over System.Net.HttpListener. Serves files from
/// a www root directory with correct MIME types, GET only, with CORS and
/// no-store cache headers. Guards against path traversal.
/// </summary>
public sealed class HttpServer : IDisposable
{
    private static readonly Dictionary<string, string> MimeTypes = new(StringComparer.OrdinalIgnoreCase)
    {
        ["html"] = "text/html; charset=utf-8",
        ["js"] = "application/javascript",
        ["mjs"] = "application/javascript",
        ["css"] = "text/css",
        ["png"] = "image/png",
        ["jpg"] = "image/jpeg",
        ["jpeg"] = "image/jpeg",
        ["webp"] = "image/webp",
        ["avif"] = "image/avif",
        ["ktx"] = "image/ktx",
        ["dds"] = "image/vnd-ms.dds",
        ["json"] = "application/json",
        ["xml"] = "application/xml",
        ["dat"] = "application/octet-stream",
        ["ogg"] = "audio/ogg",
        ["m4a"] = "audio/mp4",
        ["mp3"] = "audio/mpeg",
        ["wav"] = "audio/wav",
        ["fnt"] = "application/octet-stream",
        ["ico"] = "image/x-icon",
        ["txt"] = "text/plain; charset=utf-8",
        ["ttf"] = "font/ttf",
    };

    private readonly HttpListener _listener;
    private readonly string _wwwRoot;
    private readonly CancellationTokenSource _cts = new();
    private Task? _serveTask;

    public HttpServer(string wwwRoot, int port)
    {
        _wwwRoot = Path.GetFullPath(wwwRoot);
        _listener = new HttpListener();
        _listener.Prefixes.Add($"http://127.0.0.1:{port}/");
    }

    public void Start()
    {
        _listener.Start();
        _serveTask = Task.Run(ServeLoopAsync);
    }

    private async Task ServeLoopAsync()
    {
        while (!_cts.IsCancellationRequested)
        {
            HttpListenerContext context;
            try
            {
                context = await _listener.GetContextAsync().ConfigureAwait(false);
            }
            catch (Exception) when (_cts.IsCancellationRequested)
            {
                break;
            }
            catch (HttpListenerException)
            {
                break;
            }

            _ = Task.Run(() => HandleRequestAsync(context));
        }
    }

    private async Task HandleRequestAsync(HttpListenerContext context)
    {
        try
        {
            var request = context.Request;
            var response = context.Response;

            response.Headers["Access-Control-Allow-Origin"] = "*";
            response.Headers["Cache-Control"] = "no-store";

            if (!string.Equals(request.HttpMethod, "GET", StringComparison.OrdinalIgnoreCase))
            {
                response.StatusCode = (int)HttpStatusCode.MethodNotAllowed;
                response.Close();
                return;
            }

            string relativePath = Uri.UnescapeDataString(request.Url!.AbsolutePath).TrimStart('/');
            if (string.IsNullOrEmpty(relativePath))
            {
                relativePath = "index.html";
            }

            string fullPath = Path.GetFullPath(
                Path.Combine(_wwwRoot, relativePath.Replace('/', Path.DirectorySeparatorChar)));

            // Path traversal guard: the resolved path must stay inside www root.
            if (!fullPath.StartsWith(_wwwRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)
                || !File.Exists(fullPath))
            {
                response.StatusCode = (int)HttpStatusCode.NotFound;
                response.Close();
                return;
            }

            string extension = Path.GetExtension(fullPath).TrimStart('.');
            response.ContentType = MimeTypes.TryGetValue(extension, out string? mime)
                ? mime
                : "application/octet-stream";

            await using var stream = File.OpenRead(fullPath);
            await stream.CopyToAsync(response.OutputStream).ConfigureAwait(false);
            response.Close();
        }
        catch
        {
            // Client disconnected or the response was already closed — nothing to serve.
        }
    }

    public void Dispose()
    {
        _cts.Cancel();
        try
        {
            _listener.Stop();
        }
        catch
        {
            // Listener may already be stopped.
        }

        _listener.Close();
    }
}