namespace DeskBuddy.Firmware;

/// <summary>What a connected DeskBuddy says about itself: hello fw=, and info build= board= hw= (null: not reported).</summary>
public sealed record InstalledFirmware(string? Version, string? Build, string? Board, string? Hardware);

public enum InstallOffer
{
    Install,      // no DeskBuddy on the board yet (the board itself is identified and confirmed separately)
    Update,       // newer than the installed firmware
    Reinstall,    // the same version again (a repair)
    Downgrade,    // older: only on an explicit request, with its own confirmation
    Incompatible, // never offered
}

public sealed record InstallDecision(InstallOffer Offer, string Reason);

/// <summary>Which package a device may get, by version, board and protocol. Nothing here talks to a device.</summary>
public static class UpdatePolicy
{
    public static InstallDecision Decide(FirmwareManifest package, InstalledFirmware? installed,
        FirmwareVersion companionVersion, int companionProtocol)
    {
        if (package.MinCompanion > companionVersion)
        {
            return new(InstallOffer.Incompatible,
                $"DeskBuddy {package.Version} needs Companion {package.MinCompanion} or newer (this is {companionVersion})");
        }
        if (package.Protocol != companionProtocol)
        {
            return new(InstallOffer.Incompatible,
                $"DeskBuddy {package.Version} speaks protocol {package.Protocol}; this Companion speaks {companionProtocol}");
        }
        if (installed is null)
        {
            return new(InstallOffer.Install, $"DeskBuddy {package.Version} can be installed");
        }
        if (installed.Board is { } board && board != package.Board)
        {
            return new(InstallOffer.Incompatible, $"this DeskBuddy runs on '{board}', the package is for '{package.Board}'");
        }
        if (installed.Hardware is { } hw && hw != "ok")
        {
            return new(InstallOffer.Incompatible,
                $"the board's own hardware check reports '{hw}': it is not the hardware DeskBuddy expects");
        }
        if (FirmwareVersion.TryParse(installed.Version) is not { } current)
        {
            // Builds from before versioning report a commit (fw=57eb475): all of them are older than 1.0.0.
            return new(InstallOffer.Update,
                $"the installed firmware '{installed.Version}' is a development build without a version; {package.Version} replaces it");
        }
        var c = package.Version.CompareTo(current);
        if (c > 0)
        {
            return new(InstallOffer.Update, $"{package.Version} is newer than the installed {current}");
        }
        if (c < 0)
        {
            return new(InstallOffer.Downgrade, $"{package.Version} is older than the installed {current}");
        }
        return new(InstallOffer.Reinstall, installed.Build is { } build && build != package.Build
            ? $"{current} is installed, but from another build ({Short(build)}, the package is {Short(package.Build)})"
            : $"{current} is already installed");
    }

    private static string Short(string build) => build.Length > 7 ? build[..7] : build;
}
