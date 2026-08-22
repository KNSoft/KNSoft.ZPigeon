using System.Text;
using KNSoft.ZPigeon.Server.Managed;

namespace KNSoft.ZPigeon.Web;

internal static class RemoteCommand
{
    private const int MaximumOutput = 0x00800000;

    internal static async Task<RemoteCommandResult> RunAsync(
        NativeServer server,
        string commandLine,
        string? workingDirectory = null,
        ushort columns = 120)
    {
        await using var terminal = await server.CreateTerminalAsync(commandLine, columns, 30, workingDirectory);
        using var output = new MemoryStream();
        await foreach (var data in terminal.Output.ReadAllAsync())
        {
            if (output.Length + data.Length > MaximumOutput)
            {
                throw new InvalidDataException("远端输出超过 8 MiB 限制。");
            }
            output.Write(data.Span);
        }
        var completion = await terminal.Completion;
        return new(Encoding.UTF8.GetString(output.GetBuffer(), 0, checked((int)output.Length)), completion.Status);
    }
}

internal sealed record RemoteCommandResult(string Text, ZpStatus Status);
