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
}

/// <summary>
/// One parsed command from an input script. Unused fields default to 0.
/// </summary>
internal sealed record InputCommand(
    InputCommandKind Kind,
    int X = 0,
    int Y = 0,
    int KeyCode = 0,
    int Milliseconds = 0);

/// <summary>
/// Parses an input script file. One command per line:
///   tap &lt;x&gt; &lt;y&gt;   — pointerdown+pointerup + touchstart/touchend at (x, y)
///   move &lt;x&gt; &lt;y&gt;  — pointermove at (x, y)
///   key &lt;code&gt;    — keydown+keyup with the given numeric keyCode
///   wait &lt;ms&gt;     — pause for the given number of milliseconds
///   # comment     — ignored (blank lines too)
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
                default:
                    throw new FormatException($"Invalid input script line: '{rawLine}'");
            }
        }

        return commands;
    }
}