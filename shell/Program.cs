using System.Net;
using System.Net.Sockets;

namespace OracleShell;

/// <summary>
/// Entry point for the Oracle shell: starts the local HTTP server, swaps the
/// runner files into www/, and runs the WebView2 form. The server is stopped
/// when the form closes.
/// </summary>
internal static class Program
{
    private const string RepoMarker = "reference";

    [STAThread]
    private static void Main(string[] args)
    {
        string repoRoot = FindRepoRoot();
        string wwwRoot = Path.Combine(repoRoot, "reference", "www");
        string runnerDir = Path.Combine(repoRoot, "reference", "runner");
        string tracesDir = Path.Combine(repoRoot, "reference", "traces");

        string? inputScript = ParseInputScriptArg(args);
        int timeoutMs = ParseTimeoutArg(args);
        if (inputScript is not null)
        {
            if (!Path.IsPathRooted(inputScript))
            {
                inputScript = Path.Combine(repoRoot, inputScript);
            }

            if (!File.Exists(inputScript))
            {
                throw new FileNotFoundException($"Input script not found: {inputScript}");
            }
        }

        // Swap the WinUI bridge for the browser GameInterface (idempotent, overwrite).
        CopyRunnerFile(Path.Combine(runnerDir, "index.html"), Path.Combine(wwwRoot, "index.html"));
        CopyRunnerFile(Path.Combine(runnerDir, "microsite-game-interface.js"), Path.Combine(wwwRoot, "microsite-game-interface.js"));
        CopyRunnerFile(Path.Combine(runnerDir, "trace.js"), Path.Combine(wwwRoot, "trace.js"));

        // Phase 1 oracle instrumentation (reference/tools/trace_oracle.js),
        // injected by MainForm before any page script (required file).
        string instrumentScript = Path.Combine(repoRoot, "reference", "tools", "trace_oracle.js");
        if (!File.Exists(instrumentScript))
        {
            throw new FileNotFoundException($"Instrument script missing: {instrumentScript}");
        }

        int port = PickFreePort();
        using var server = new HttpServer(wwwRoot, port);
        server.Start();

        string url = $"http://127.0.0.1:{port}/";
        Console.WriteLine($"[shell] serving {wwwRoot} at {url}");

        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm(url, tracesDir, inputScript, instrumentScript, timeoutMs));

        Console.WriteLine("[shell] form closed, stopping server");
    }

    private static int ParseTimeoutArg(string[] args)
    {
        for (int i = 0; i < args.Length - 1; i++)
        {
            if (string.Equals(args[i], "--timeout-ms", StringComparison.OrdinalIgnoreCase)
                && int.TryParse(args[i + 1], out int ms) && ms > 0)
            {
                return ms;
            }
        }

        return 150000;
    }

    private static string? ParseInputScriptArg(string[] args)
    {
        for (int i = 0; i < args.Length - 1; i++)
        {
            if (string.Equals(args[i], "--input-script", StringComparison.OrdinalIgnoreCase))
            {
                return args[i + 1];
            }
        }

        return null;
    }

    private static void CopyRunnerFile(string source, string destination)
    {
        if (!File.Exists(source))
        {
            throw new FileNotFoundException($"Runner file missing: {source}");
        }

        File.Copy(source, destination, overwrite: true);
        Console.WriteLine($"[shell] copied {source} -> {destination}");
    }

    private static int PickFreePort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }

    private static string FindRepoRoot()
    {
        // Walk up from the executable location until we find the repo marker.
        string? dir = AppContext.BaseDirectory;
        while (dir is not null)
        {
            if (Directory.Exists(Path.Combine(dir, RepoMarker, "www")))
            {
                return dir;
            }

            dir = Path.GetDirectoryName(dir);
        }

        throw new DirectoryNotFoundException(
            $"Could not locate repo root (looking for '{RepoMarker}\\www') above {AppContext.BaseDirectory}");
    }
}