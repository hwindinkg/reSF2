namespace OracleShell;

/// <summary>
/// Kinds of commands understood by the --input-script driver.
/// </summary>
internal enum InputCommandKind
{
    Tap,
    Move,
    Key,
    Wait,
    /// <summary>Screenshot verb for the UI-diff harness
    /// (<c>shot &lt;name&gt;</c> — captures ui/oracle_&lt;name&gt;.png).</summary>
    Shot,
    /// <summary>
    /// Frame-anchored command, executed page-side at an exact fight.frame
    /// (see trace_oracle.js stimulus driver). InnerKind says Tap, Key or
    /// Drag; Milliseconds carries the frame number; X/Y carry tap coords,
    /// key code (KeyCode), or drag start (X2/Y2 carry the drag end).
    /// </summary>
    AtFrame,
    /// <summary>Drag inner kind for AtFrame (X/Y start, X2/Y2 end).</summary>
    Drag,
    /// <summary>Exact-replay press (KeyCode=control, X=index, Y=type).</summary>
    Press,
    /// <summary>Exact-replay release (KeyCode=control, X=index, Y=type).</summary>
    Release,
}

/// <summary>
/// One parsed command from an input script. Unused fields default to 0.
/// </summary>
internal sealed record InputCommand(
    InputCommandKind Kind,
    int X = 0,
    int Y = 0,
    int KeyCode = 0,
    int Milliseconds = 0,
    InputCommandKind InnerKind = InputCommandKind.Tap,
    int X2 = 0,
    int Y2 = 0,
    string ShotName = "");

/// <summary>
/// Parses an input script file. One command per line:
///   tap &lt;x&gt; &lt;y&gt;   — pointerdown+pointerup + touchstart/touchend at (x, y)
///   move &lt;x&gt; &lt;y&gt;  — pointermove at (x, y)
///   key &lt;code&gt;    — keydown+keyup with the given numeric keyCode
///   wait &lt;ms&gt;     — pause for the given number of milliseconds
///   shot &lt;name&gt;   — screenshot to ui/oracle_&lt;name&gt;.png (UI harness)
///   atframe &lt;f&gt; tap &lt;x&gt; &lt;y&gt; — page-side tap at exact fight.frame f
///   atframe &lt;f&gt; key &lt;code&gt;        — page-side key at exact fight.frame f
///   atframe &lt;f&gt; drag &lt;x1&gt; &lt;y1&gt; &lt;x2&gt; &lt;y2&gt; — page-side joystick drag
///   atframe &lt;f&gt; press &lt;c&gt; &lt;x&gt; &lt;t&gt; — exact replay: fight.N0a(control c)
///   atframe &lt;f&gt; release &lt;c&gt; &lt;x&gt; &lt;t&gt; — exact replay: fight.O0a(control c)
///   # comment     — ignored (blank lines too)
/// atframe items are forwarded to the page once (window.__oracleStimulus)
/// and skipped by the wall-clock timer: landing frames are exact.
/// </summary>
internal static class InputScriptParser
{
    public static List<InputCommand> Parse(string path)
    {
        var commands = new List<InputCommand>();
        foreach (string rawLine in File.ReadAllLines(path))
        {
            string line = rawLine.Trim();
            if (line.Length == 0 || line.StartsWith('#'))
            {
                continue;
            }

            string[] parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            switch (parts[0])
            {
                case "tap" when parts.Length >= 3
                    && int.TryParse(parts[1], out int tapX) && int.TryParse(parts[2], out int tapY):
                    commands.Add(new InputCommand(InputCommandKind.Tap, X: tapX, Y: tapY));
                    break;
                case "move" when parts.Length >= 3
                    && int.TryParse(parts[1], out int moveX) && int.TryParse(parts[2], out int moveY):
                    commands.Add(new InputCommand(InputCommandKind.Move, X: moveX, Y: moveY));
                    break;
                case "key" when parts.Length >= 2 && int.TryParse(parts[1], out int keyCode):
                    commands.Add(new InputCommand(InputCommandKind.Key, KeyCode: keyCode));
                    break;
                case "wait" when parts.Length >= 2 && int.TryParse(parts[1], out int waitMs):
                    commands.Add(new InputCommand(InputCommandKind.Wait, Milliseconds: waitMs));
                    break;
                case "shot" when parts.Length >= 2 && parts[1].Length > 0:
                    commands.Add(new InputCommand(InputCommandKind.Shot,
                        ShotName: parts[1]));
                    break;
                case "atframe" when parts.Length >= 5 && int.TryParse(parts[1], out int frame)
                    && string.Equals(parts[2], "tap", StringComparison.OrdinalIgnoreCase)
                    && int.TryParse(parts[3], out int afTapX) && int.TryParse(parts[4], out int afTapY):
                    commands.Add(new InputCommand(InputCommandKind.AtFrame, X: afTapX, Y: afTapY,
                        Milliseconds: frame, InnerKind: InputCommandKind.Tap));
                    break;
                case "atframe" when parts.Length >= 4 && int.TryParse(parts[1], out int keyFrame)
                    && string.Equals(parts[2], "key", StringComparison.OrdinalIgnoreCase)
                    && int.TryParse(parts[3], out int afKeyCode):
                    commands.Add(new InputCommand(InputCommandKind.AtFrame, KeyCode: afKeyCode,
                        Milliseconds: keyFrame, InnerKind: InputCommandKind.Key));
                    break;
                case "atframe" when parts.Length >= 7 && int.TryParse(parts[1], out int dragFrame)
                    && string.Equals(parts[2], "drag", StringComparison.OrdinalIgnoreCase)
                    && int.TryParse(parts[3], out int dragX1) && int.TryParse(parts[4], out int dragY1)
                    && int.TryParse(parts[5], out int dragX2) && int.TryParse(parts[6], out int dragY2):
                    commands.Add(new InputCommand(InputCommandKind.AtFrame, X: dragX1, Y: dragY1,
                        Milliseconds: dragFrame, InnerKind: InputCommandKind.Drag,
                        X2: dragX2, Y2: dragY2));
                    break;
                case "atframe" when parts.Length >= 6 && int.TryParse(parts[1], out int prFrame)
                    && (string.Equals(parts[2], "press", StringComparison.OrdinalIgnoreCase)
                        || string.Equals(parts[2], "release", StringComparison.OrdinalIgnoreCase))
                    && int.TryParse(parts[3], out int prControl) && int.TryParse(parts[4], out int prIndex)
                    && int.TryParse(parts[5], out int prType):
                    commands.Add(new InputCommand(InputCommandKind.AtFrame, KeyCode: prControl, X: prIndex,
                        Y: prType, Milliseconds: prFrame,
                        InnerKind: string.Equals(parts[2], "press", StringComparison.OrdinalIgnoreCase)
                            ? InputCommandKind.Press : InputCommandKind.Release));
                    break;
                default:
                    throw new FormatException($"Invalid input script line: '{rawLine}'");
            }
        }

        return commands;
    }
}