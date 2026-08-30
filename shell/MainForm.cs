using System.Text.Json;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace OracleShell;

/// <summary>
/// Main window: hosts the WebView2 control, logs all console messages to
/// reference/traces/console.log, and captures boot screenshots at ~8s and ~20s
/// after navigation.
///
/// Note: the CoreWebView2.ConsoleMessage event was removed from the .NET SDK
/// projection (WebView2 SDK 1.0.4xxx). Console capture is done via the
/// DevTools Protocol events Runtime.consoleAPICalled (console.* calls) and
/// Runtime.exceptionThrown (uncaught exceptions), which together cover all
/// console messages.
/// </summary>
public sealed class MainForm : Form
{
    private const int FirstScreenshotDelayMs = 8000;
    private const int SecondScreenshotDelayMs = 20000;

    private readonly WebView2 _webView = new();
    private readonly string _tracesDir;
    private readonly string _consoleLogPath;
    private readonly object _consoleLock = new();

    public MainForm(string url, string tracesDir)
    {
        _tracesDir = tracesDir;
        _consoleLogPath = Path.Combine(tracesDir, "console.log");

        Text = "reSF2 Oracle";
        ClientSize = new Size(1280, 720);
        BackColor = Color.FromArgb(16, 16, 16);
        StartPosition = FormStartPosition.CenterScreen;

        _webView.Dock = DockStyle.Fill;
        _webView.DefaultBackgroundColor = Color.FromArgb(16, 16, 16);
        Controls.Add(_webView);

        Load += async (_, _) => await InitializeWebViewAsync(url);
    }

    private async Task InitializeWebViewAsync(string url)
    {
        Directory.CreateDirectory(_tracesDir);

        try
        {
            await _webView.EnsureCoreWebView2Async();

            _webView.CoreWebView2.Settings.AreDefaultContextMenusEnabled = false;
            _webView.CoreWebView2.NavigationCompleted += OnNavigationCompleted;

            // Capture console messages via DevTools Protocol (ConsoleMessage
            // event no longer exists in this SDK). Enable before navigating so
            // early boot messages are not missed.
            var consoleReceiver = _webView.CoreWebView2.GetDevToolsProtocolEventReceiver("Runtime.consoleAPICalled");
            consoleReceiver.DevToolsProtocolEventReceived += OnConsoleMessage;
            var exceptionReceiver = _webView.CoreWebView2.GetDevToolsProtocolEventReceiver("Runtime.exceptionThrown");
            exceptionReceiver.DevToolsProtocolEventReceived += OnExceptionThrown;
            await _webView.CoreWebView2.CallDevToolsProtocolMethodAsync("Runtime.enable", "{}");

            _webView.CoreWebView2.Navigate(url);
        }
        catch (Exception ex)
        {
            AppendConsoleLine($"[shell] WebView2 init failed: {ex.Message}");
            MessageBox.Show($"WebView2 initialization failed:\n{ex.Message}", "reSF2 Oracle",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void OnConsoleMessage(object? sender, CoreWebView2DevToolsProtocolEventReceivedEventArgs e)
    {
        // ParameterObjectAsJson: {"type":"log|warning|error|debug|info","args":[...],...}
        string level = "log";
        string message = e.ParameterObjectAsJson;
        try
        {
            using var doc = JsonDocument.Parse(e.ParameterObjectAsJson);
            var root = doc.RootElement;
            if (root.TryGetProperty("type", out JsonElement typeElement))
            {
                level = typeElement.GetString() ?? "log";
            }

            if (root.TryGetProperty("args", out JsonElement argsElement)
                && argsElement.ValueKind == JsonValueKind.Array)
            {
                var parts = new List<string>();
                foreach (JsonElement arg in argsElement.EnumerateArray())
                {
                    if (arg.TryGetProperty("value", out JsonElement valueElement))
                    {
                        parts.Add(valueElement.ToString());
                    }
                    else if (arg.TryGetProperty("description", out JsonElement descriptionElement))
                    {
                        parts.Add(descriptionElement.ToString());
                    }
                    else
                    {
                        parts.Add(arg.ToString());
                    }
                }

                message = string.Join(" ", parts);
            }
        }
        catch (JsonException)
        {
            // Keep the raw JSON payload as the message.
        }

        AppendConsoleLine($"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] [{level}] {message}");
    }

    private void OnExceptionThrown(object? sender, CoreWebView2DevToolsProtocolEventReceivedEventArgs e)
    {
        // ParameterObjectAsJson: {"exceptionDetails":{"text":"...","exception":{...}},...}
        string message = e.ParameterObjectAsJson;
        try
        {
            using var doc = JsonDocument.Parse(e.ParameterObjectAsJson);
            if (doc.RootElement.TryGetProperty("exceptionDetails", out JsonElement details)
                && details.TryGetProperty("text", out JsonElement textElement))
            {
                message = textElement.GetString() ?? message;
            }
        }
        catch (JsonException)
        {
            // Keep the raw JSON payload as the message.
        }

        AppendConsoleLine($"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] [error] {message}");
    }

    private async void OnNavigationCompleted(object? sender, CoreWebView2NavigationCompletedEventArgs e)
    {
        if (!e.IsSuccess)
        {
            AppendConsoleLine($"[shell] navigation failed: {e.WebErrorStatus}");
            return;
        }

        AppendConsoleLine($"[shell] navigation completed: {_webView.CoreWebView2.Source}");

        // Screenshot 1 at ~8s after navigation, screenshot 2 at ~20s.
        await Task.Delay(FirstScreenshotDelayMs);
        await CaptureScreenshotAsync("boot.png");

        await Task.Delay(SecondScreenshotDelayMs - FirstScreenshotDelayMs);
        await CaptureScreenshotAsync("boot2.png");
    }

    private async Task CaptureScreenshotAsync(string fileName)
    {
        string path = Path.Combine(_tracesDir, fileName);
        try
        {
            await using var stream = File.Create(path);
            await _webView.CoreWebView2.CapturePreviewAsync(CoreWebView2CapturePreviewImageFormat.Png, stream);
            AppendConsoleLine($"[shell] screenshot saved: {path} ({new FileInfo(path).Length} bytes)");
        }
        catch (Exception ex)
        {
            AppendConsoleLine($"[shell] screenshot failed: {ex.Message}");
        }
    }

    private void AppendConsoleLine(string line)
    {
        lock (_consoleLock)
        {
            File.AppendAllText(_consoleLogPath, line + Environment.NewLine);
        }
    }
}