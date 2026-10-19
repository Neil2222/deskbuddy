using System.Diagnostics;
using System.Net;
using System.Text;

namespace DeskBuddy.Companion.Usage;

/// <summary>
/// Sends requests through Windows' own <c>curl.exe</c> (System32, part of Windows 10 1803 and later). claude.ai sits
/// behind Cloudflare, which answers .NET's TLS handshake (HttpClient and Invoke-WebRequest alike) with a 403
/// "Just a moment..." challenge, while curl.exe gets the real answer. The request (URL and headers, so also the session
/// key cookie) goes to curl over stdin as a config file, never on the command line where other programs could read it.
/// </summary>
public sealed class CurlHandler : HttpMessageHandler
{
    private const int TimedOut = 28; // curl's exit code for --max-time

    public static string CurlPath => Path.Combine(Environment.SystemDirectory, "curl.exe");

    protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
    {
        var config = new StringBuilder();
        config.Append("url = ").Append(Quote(request.RequestUri!.AbsoluteUri)).Append('\n');
        config.Append("request = ").Append(Quote(request.Method.Method)).Append('\n');
        var headers = request.Headers.AsEnumerable();
        if (request.Content is not null)
        {
            headers = headers.Concat(request.Content.Headers.Where(h => !h.Key.Equals("Content-Length", StringComparison.OrdinalIgnoreCase)));
        }
        foreach (var (name, values) in headers)
        {
            config.Append("header = ").Append(Quote($"{name}: {string.Join(", ", values)}")).Append('\n');
        }

        var start = new ProcessStartInfo(CurlPath)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        foreach (var arg in new[] { "--silent", "--show-error", "--include", "--max-time", "15", "--config", "-" })
        {
            start.ArgumentList.Add(arg);
        }
        Process process;
        try
        {
            process = Process.Start(start) ?? throw new HttpRequestException("curl.exe did not start");
        }
        catch (System.ComponentModel.Win32Exception e)
        {
            throw new HttpRequestException($"cannot start {CurlPath}", e);
        }
        using (process)
        {
            try
            {
                var output = new MemoryStream();
                var reading = process.StandardOutput.BaseStream.CopyToAsync(output, cancellationToken);
                var errors = process.StandardError.ReadToEndAsync(cancellationToken);
                await process.StandardInput.WriteAsync(config.ToString().AsMemory(), cancellationToken);
                process.StandardInput.Close();
                await reading;
                await errors;
                await process.WaitForExitAsync(cancellationToken);
                if (process.ExitCode == TimedOut)
                {
                    throw new TaskCanceledException("curl.exe: no answer in time");
                }
                if (process.ExitCode != 0)
                {
                    throw new HttpRequestException($"curl.exe failed (exit code {process.ExitCode})");
                }
                return Parse(output.ToArray(), request);
            }
            catch (OperationCanceledException)
            {
                Kill(process);
                throw;
            }
        }
    }

    // --include: the status line and headers, a blank line, then the body.
    private static HttpResponseMessage Parse(byte[] raw, HttpRequestMessage request)
    {
        var end = raw.AsSpan().IndexOf("\r\n\r\n"u8);
        if (end < 0)
        {
            throw new HttpRequestException("curl.exe: no HTTP answer");
        }
        var lines = Encoding.ASCII.GetString(raw, 0, end).Split("\r\n");
        var status = lines[0].Split(' ', 3);
        if (status.Length < 2 || !int.TryParse(status[1], out var code))
        {
            throw new HttpRequestException("curl.exe: no HTTP status");
        }
        var response = new HttpResponseMessage((HttpStatusCode)code)
        {
            RequestMessage = request,
            Content = new ByteArrayContent(raw, end + 4, raw.Length - end - 4),
        };
        foreach (var line in lines.Skip(1))
        {
            var colon = line.IndexOf(':');
            if (colon <= 0)
            {
                continue;
            }
            var name = line[..colon].Trim();
            var value = line[(colon + 1)..].Trim();
            if (!response.Headers.TryAddWithoutValidation(name, value))
            {
                response.Content.Headers.TryAddWithoutValidation(name, value);
            }
        }
        return response;
    }

    // A curl config string: double quotes, with \ and " escaped; control characters cannot be passed.
    private static string Quote(string value)
    {
        var quoted = new StringBuilder("\"");
        foreach (var c in value)
        {
            if (char.IsControl(c))
            {
                continue;
            }
            if (c is '\\' or '"')
            {
                quoted.Append('\\');
            }
            quoted.Append(c);
        }
        return quoted.Append('"').ToString();
    }

    private static void Kill(Process process)
    {
        try
        {
            process.Kill();
        }
        catch (InvalidOperationException)
        {
        }
    }
}
