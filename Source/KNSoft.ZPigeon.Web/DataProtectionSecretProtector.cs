using KNSoft.ZPigeon.Agent;
using Microsoft.AspNetCore.DataProtection;

namespace KNSoft.ZPigeon.Web;

internal sealed class DataProtectionSecretProtector(IDataProtectionProvider provider) : ISecretProtector
{
    private readonly IDataProtector protector = provider.CreateProtector("KNSoft.ZPigeon.AgentSecrets.v1");

    public string Protect(string value) => protector.Protect(value);

    public string Unprotect(string value) => protector.Unprotect(value);
}
