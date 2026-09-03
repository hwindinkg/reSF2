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
    private const int DefaultInputTickMs = 50;
    private const int TapHoldMs = 120;

    private readonly WebView2 _webView = new();
    private readonly string _tracesDir;
    private readonly string _consoleLogPath;
    private readonly object _consoleLock = new();
    private readonly string? _inputScriptPath;
    private readonly string _instrumentScriptPath;
    private readonly System.Windows.Forms.Timer _inputTimer = new();
    private readonly System.Windows.Forms.Timer _completionTimer = new();
    private List<InputCommand>? _inputCommands;
    private int _inputCommandIndex;
    private DateTime _navigationTime;
    private const int CompletionPollMs = 2000;
    private const int CompletionTimeoutMs = 150000;

    public MainForm(string url, string tracesDir, string? inputScriptPath, string instrumentScriptPath)
    {
        _tracesDir = tracesDir;
        _consoleLogPath = Path.Combine(tracesDir, "console.log");
        _inputScriptPath = inputScriptPath;
        _instrumentScriptPath = instrumentScriptPath;
        _inputTimer.Tick += OnInputTimerTick;
        _completionTimer.Interval = CompletionPollMs;
        _completionTimer.Tick += OnCompletionTimerTick;

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
            // Fresh browser profile per run: clean localStorage/saves every
            // time, so repeated runs start from identical game state
            // (deterministic oracle traces, Phase 1 gate).
            string profileDir = Path.Combine(Path.GetTempPath(), "reSF2-oracle-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(profileDir);
            var env = await CoreWebView2Environment.CreateAsync(null, profileDir);
            AppendConsoleLine($"[shell] webview profile: {profileDir}");
            await _webView.EnsureCoreWebView2Async(env);

            // Phase 1 oracle instrumentation (reference/tools/trace_oracle.js):
            // runs BEFORE any page script, i.e. before the game boots.
            string instrumentScript = File.ReadAllText(_instrumentScriptPath);
            await _webView.CoreWebView2.AddScriptToExecuteOnDocumentCreatedAsync(instrumentScript);
            AppendConsoleLine($"[shell] instrument script injected: {_instrumentScriptPath} ({instrumentScript.Length} chars)");

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
        _navigationTime = DateTime.UtcNow;
        _completionTimer.Start();

        if (_inputScriptPath is not null)
        {
            StartInputScript(_inputScriptPath);
        }

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

    private void StartInputScript(string path)
    {
        try
        {
            _inputCommands = InputScriptParser.Parse(path);
        }
        catch (Exception ex)
        {
            AppendConsoleLine($"[shell] input script load failed: {ex.Message}");
            return;
        }

        _inputCommandIndex = 0;
        AppendConsoleLine($"[shell] input script loaded: {path} ({_inputCommands.Count} commands)");
        _inputTimer.Interval = DefaultInputTickMs;
        _inputTimer.Start();
    }

    private void OnInputTimerTick(object? sender, EventArgs e)
    {
        _inputTimer.Interval = DefaultInputTickMs;

        if (_inputCommands is null || _inputCommandIndex >= _inputCommands.Count)
        {
            _inputTimer.Stop();
            AppendConsoleLine("[shell] input script finished");
            return;
        }

        InputCommand command = _inputCommands[_inputCommandIndex++];
        switch (command.Kind)
        {
            case InputCommandKind.Wait:
                _inputTimer.Interval = Math.Max(command.Milliseconds, 1);
                break;
            case InputCommandKind.Tap:
                _ = DispatchTapAsync(command.X, command.Y);
                break;
            case InputCommandKind.Move:
                _ = DispatchMoveAsync(command.X, command.Y);
                break;
            case InputCommandKind.Key:
                _ = DispatchKeyAsync(command.KeyCode);
                break;
        }
    }

    private async Task DispatchTapAsync(int x, int y)
    {
        AppendConsoleLine($"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] [INPUT] tap {x} {y}");
        await ExecuteInputScriptAsync(BuildPointerEventScript(x, y, "pointerdown"));
        await ExecuteInputScriptAsync(BuildTouchEventScript(x, y, "touchstart"));
        // The game's button state (dy) reports a press as "down" for exactly one
        // frame poll; a synchronous down+up is never observed. Hold the press
        // across a few frames so the fight screen sees Db(0) and moves/attacks.
        await Task.Delay(TapHoldMs);
        await ExecuteInputScriptAsync(BuildPointerEventScript(x, y, "pointerup"));
        await ExecuteInputScriptAsync(BuildTouchEventScript(x, y, "touchend"));
    }

    private async Task DispatchMoveAsync(int x, int y)
    {
        AppendConsoleLine($"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] [INPUT] move {x} {y}");
        await ExecuteInputScriptAsync(BuildPointerEventScript(x, y, "pointermove"));
    }

    private async Task DispatchKeyAsync(int keyCode)
    {
        AppendConsoleLine($"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] [INPUT] key {keyCode}");
        await ExecuteInputScriptAsync(BuildKeyEventScript(keyCode, "keydown"));
        // Same press-hold requirement as taps: the game's button state only
        // reports a press as "down" for one frame poll, so hold across a few.
        await Task.Delay(TapHoldMs);
        await ExecuteInputScriptAsync(BuildKeyEventScript(keyCode, "keyup"));
    }

    private async Task ExecuteInputScriptAsync(string script)
    {
        try
        {
            await _webView.CoreWebView2.ExecuteScriptAsync(script);
        }
        catch (Exception ex)
        {
            AppendConsoleLine($"[shell] input dispatch failed: {ex.Message}");
        }
    }

    private static string BuildPointerEventScript(int x, int y, string type)
    {
        // The game attaches its pointer/touch listeners to the main canvas
        // (#gfx, created by the runtime), not to window/document. Dispatching
        // on the canvas reaches those listeners and bubbles up to document and
        // window, so window/document listeners fire too.
        return $@"
(function() {{
    var target = document.getElementById('gfx') || document;
    var opts = {{ bubbles: true, cancelable: true, clientX: {x}, clientY: {y}, pointerId: 1, pointerType: 'touch', isPrimary: true }};
    var evt = new PointerEvent('{type}', opts);
    target.dispatchEvent(evt);
}})();";
    }

    private static string BuildTouchEventScript(int x, int y, string type)
    {
        // TouchEventInit requires real Touch instances (plain objects are
        // rejected by the constructor), so build one via new Touch(...).
        return $@"
(function() {{
    var target = document.getElementById('gfx') || document;
    var touch = new Touch({{ identifier: 1, target: target, clientX: {x}, clientY: {y} }});
    var opts = {{ bubbles: true, cancelable: true, touches: [touch], targetTouches: [touch], changedTouches: [touch] }};
    var evt = new TouchEvent('{type}', opts);
    target.dispatchEvent(evt);
}})();";
    }

    private static string BuildKeyEventScript(int keyCode, string type)
    {
        return $@"
(function() {{
    var target = document.getElementById('gfx') || document;
    var opts = {{ bubbles: true, cancelable: true, keyCode: {keyCode}, which: {keyCode} }};
    var evt = new KeyboardEvent('{type}', opts);
    target.dispatchEvent(evt);
}})();";
    }

    private async void OnCompletionTimerTick(object? sender, EventArgs e)
    {
        // Safety timeout: close anyway so automated runs always terminate.
        if ((DateTime.UtcNow - _navigationTime).TotalMilliseconds >= CompletionTimeoutMs)
        {
            _completionTimer.Stop();
            AppendConsoleLine("[shell] TIMEOUT waiting for oracle done, closing");
            Close();
            return;
        }

        bool done = await QueryOracleDoneAsync();
        if (done)
        {
            _completionTimer.Stop();
            AppendConsoleLine("[shell] oracle trace done, closing");
            Close();
        }
    }

    private async Task<bool> QueryOracleDoneAsync()
    {
        const string script = "(function(){try{return window.__oracleDone===true||(window.__sf2Trace&&window.__sf2Trace.state().done===true)}catch(e){return false}})()";
        try
        {
            string result = await _webView.CoreWebView2.ExecuteScriptAsync(script);
            return string.Equals(result.Trim(), "true", StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception ex)
        {
            AppendConsoleLine($"[shell] completion query failed: {ex.Message}");
            return false;
        }
    }
}